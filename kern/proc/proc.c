/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

// For use for the proc array
#define PROCINLINE

#include <types.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <syscall.h>
#include <bitmap.h>
#include <array.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Mechanism for making the kernel menu thread sleep while processes are running
 */
#ifdef UW
/* count of the number of processes, excluding kproc */
static unsigned int proc_count;
/* provides mutual exclusion for proc_count */
/* it would be better to use a lock here, but we use a semaphore because locks are not implemented in the base kernel */ 
static struct semaphore *proc_count_mutex;
/* used to signal the kernel menu thread when there are no processes */
struct semaphore *no_proc_sem;   
#endif  // UW



/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	// Initialize the proc_exit_lock
	proc->proc_exit_lock = lock_create(name);
	if (proc->proc_exit_lock == NULL) {
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	// Initialize the lock for the children array
 	proc->proc_children_lock = lock_create(name);
 	if (proc->proc_children_lock == NULL) {
		 lock_destroy(proc->proc_exit_lock);
		 kfree(proc->p_name);
		 kfree(proc);
		 return NULL;
	}

	// Initialize the condition variable that will be needed for waitpid
	proc->proc_exit_cv = cv_create(name);
	if (proc->proc_exit_cv == NULL) {
		lock_destroy(proc->proc_exit_lock);
		lock_destroy(proc->proc_children_lock);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	int rtn_val = proc_assign_pid((pid_t*)&proc->pid);
	if (rtn_val) {
		lock_destroy(proc->proc_exit_lock);
		lock_destroy(proc->proc_children_lock);
		cv_destroy(proc->proc_exit_cv);
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}


	threadarray_init(&proc->p_threads);
	// Initialize the proc array siilar to threadarray
	procarray_init(&proc->proc_children);
	spinlock_init(&proc->p_lock);
	
	// Proc has just been created, so set the exited predicate to false
	proc->proc_exited = false;
	proc->proc_parent_exited = false;
	proc->proc_exit_status = 0;
	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

#ifdef UW
	proc->console = NULL;
#endif // UW

	return proc;
}

/*
 * Destroy a proc structure.
 */
void
proc_destroy(struct proc *proc)
{
	/*
         * note: some parts of the process structure, such as the address space,
         *  are destroyed in sys_exit, before we get here
         *
         * note: depending on where this function is called from, curproc may not
         * be defined because the calling thread may have already detached itself
         * from the process.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}


#ifndef UW  // in the UW version, space destruction occurs in sys_exit, not here
	if (proc->p_addrspace) {
		/*
		 * In case p is the currently running process (which
		 * it might be in some circumstances, or if this code
		 * gets moved into exit as suggested above), clear
		 * p_addrspace before calling as_destroy. Otherwise if
		 * as_destroy sleeps (which is quite possible) when we
		 * come back we'll be calling as_activate on a
		 * half-destroyed address space. This tends to be
		 * messily fatal.
		 */
		struct addrspace *as;

		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}
#endif // UW

#ifdef UW
	if (proc->console) {
	  vfs_close(proc->console);
	}
#endif // UW

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);
	//Cleanup all proc stuff for A2
	int numChildren = procarray_num(&proc->proc_children);
		for (int i = numChildren - 1; i >= 0; i--) {
		procarray_remove(&proc->proc_children, i);
	}
	procarray_cleanup(&proc->proc_children);
	lock_destroy(proc->proc_exit_lock);
	lock_destroy(proc->proc_children_lock);
	cv_destroy(proc->proc_exit_cv);

	// Proc is being destroyed, so now the pid can be re-used
	proc_set_pid_unused(proc->pid);

	kfree(proc->p_name);
	kfree(proc);

#ifdef UW
	/* decrement the process count */
        /* note: kproc is not included in the process count, but proc_destroy
	   is never called on kproc (see KASSERT above), so we're OK to decrement
	   the proc_count unconditionally here */
	P(proc_count_mutex); 
	KASSERT(proc_count > 0);
	proc_count--;
	/* signal the kernel menu thread if the process count has reached zero */
	if (proc_count == 0) {
	  V(no_proc_sem);
	}
	V(proc_count_mutex);
#endif // UW
	

}



// Sets the exit status of the process to true and encodes the exit status
void proc_set_exit_status(struct proc * proc, const int exitcode, const int type) {
	switch(type) {
		case __WEXITED:
		proc->proc_exit_status = _MKWAIT_EXIT(exitcode);
		break;
		case __WSIGNALED:
		proc->proc_exit_status = _MKWAIT_SIG(exitcode);
		break;
		case __WCORED:
		proc->proc_exit_status = _MKWAIT_CORE(exitcode);
		break;
		case __WSTOPPED:
		proc->proc_exit_status = _MKWAIT_STOP(exitcode);
		break;
	}
	proc->proc_exited = true;
}



// Assigns the next avail. pid to the process
int proc_assign_pid(int * proc_pid_addr_ptr) {
	if (proc_pid_addr_ptr == NULL) {
		return EINVAL;
	}else {
		spinlock_acquire(&proc_id_map_spinlock);
		int rtn = bitmap_alloc(proc_id_map, (unsigned*)proc_pid_addr_ptr);
		spinlock_release(&proc_id_map_spinlock);
		return rtn;
	}
}

// Sets the given pid to unused, so that it can be assigned later on.
void proc_set_pid_unused(const int pid) {
	KASSERT(pid > 0 && pid < PID_MAX);
	spinlock_acquire(&proc_id_map_spinlock);
	bitmap_unmark(proc_id_map, pid);
	spinlock_release(&proc_id_map_spinlock);
}

/*
 * Finds the children of the given process proc that matches the process
 * id child_pid, and returns it. If there is no child with the given pid,
 * returns NULL.
 */
struct proc * proc_find_child(struct proc * proc, const int child_pid) {
	struct proc * child_proc = NULL;
	if (proc == NULL) {
		return NULL;
	}
	// Acquire the lock for the children of the given process
	lock_acquire(proc->proc_children_lock);
	// Get the number of children this process has
	int num_children = procarray_num(&proc->proc_children);
	for (int i = 0; i < num_children; i++) {
		struct proc * child = procarray_get(&proc->proc_children, i);
		if (child->pid == child_pid) {
			child_proc = child;
			break;
		}
	}
	lock_release(proc->proc_children_lock);
	return child_proc;
}


/*
 * Called when a process exits.
 * Notifies all the children that the parent has exited, and
 * destroys any children that were semi-destroyed in the past, since now
 * that the parent is destroyed, there is no relationship.
 */
void proc_exited_signal(struct proc *proc) {
	lock_acquire(proc->proc_children_lock);
	unsigned int i;
	for (i=0; i < procarray_num(&proc->proc_children); i++) {
		struct proc * child = procarray_get(&proc->proc_children, i);
		lock_acquire(child->proc_exit_lock);
		if (child->proc_exited) {
			// Destroy all the semi-destroyed children, since there is no
			// longer a parent-child relationship or interest.
			lock_release(child->proc_exit_lock);
			proc_destroy(child);
		} else {
			child->proc_parent_exited = true;
			lock_release(child->proc_exit_lock);
		}
	}
	lock_release(proc->proc_children_lock);
}

/*
 * When a process calls waitpid on pid, this function checks to make sure that
 * a process with pid exists , and
 * the process is a child of the process calling waitpid
 */
int waitpid_interested_error(const int pid) {
	if ( pid <= 0 || pid >= PID_MAX){
		return EINVAL;
	}
	spinlock_acquire(&proc_id_map_spinlock);
	int is_set = bitmap_isset(proc_id_map, pid);
	spinlock_release(&proc_id_map_spinlock);
	return is_set? ECHILD:ESRCH;
}


/*
 * Semi-destroys the process. This is required because once a child process
 * exits, the parent child may still need its exit code, so we do not completely
 * destroy a process once a process exits. We leave it to the parent upon exit.
 * Once the parent process exits, the parent process
 * will ensure to fully destroy all the semi-destroyed children.
 * This mechanism is useful for waitpid, but other approaches are possible.
 */
void proc_semi_destroy(struct proc *proc) {
	
	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
	
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}



	if (proc->p_addrspace) {
		struct addrspace *as;
		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}


	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);
// NOTICE how we do not clean up the entire process
//	P(proc_count_mutex);
//        KASSERT(proc_count > 0);
//        proc_count--;
        /* signal the kernel menu thread if the process count has reached zero */
//        if (proc_count == 0) {
//          V(no_proc_sem);
//        }
//        V(proc_count_mutex);
}


/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
  // Before creating the kernel process, initialize the pid_bitmap
  // so that the kernel proc can be assigned a pid starting at 0
  spinlock_init(&proc_id_map_spinlock);
  proc_id_map = bitmap_create(PID_MAX);
  if (proc_id_map == NULL) {
	panic("failed to initialize the pid map for pid generation\n");
  }
  // Ready to create the kernel process
  kproc = proc_create("[kernel]");
  if (kproc == NULL) {
    panic("proc_create for kproc failed\n");
  }
#ifdef UW
  proc_count = 0;
  proc_count_mutex = sem_create("proc_count_mutex",1);
  if (proc_count_mutex == NULL) {
    panic("could not create proc_count_mutex semaphore\n");
  }
  no_proc_sem = sem_create("no_proc_sem",0);
  if (no_proc_sem == NULL) {
    panic("could not create no_proc_sem semaphore\n");
  }
#endif // UW 
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *proc;
	char *console_path;

	proc = proc_create(name);
	if (proc == NULL) {
		return NULL;
	}

#ifdef UW
	/* open the console - this should always succeed */
	console_path = kstrdup("con:");
	if (console_path == NULL) {
	  panic("unable to copy console path name during process creation\n");
	}
	if (vfs_open(console_path,O_WRONLY,0,&(proc->console))) {
	  panic("unable to open the console during process creation\n");
	}
	kfree(console_path);
#endif // UW
	  
	/* VM fields */

	proc->p_addrspace = NULL;

	/* VFS fields */

#ifdef UW
	/* we do not need to acquire the p_lock here, the running thread should
           have the only reference to this process */
        /* also, acquiring the p_lock is problematic because VOP_INCREF may block */
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
#else // UW
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);
#endif // UW

#ifdef UW
	/* increment the count of processes */
        /* we are assuming that all procs, including those created by fork(),
           are created using a call to proc_create_runprogram  */
	P(proc_count_mutex); 
	proc_count++;
	V(proc_count_mutex);
#endif // UW

	// Add this new proc as a child to the parent proc
	lock_acquire(curproc->proc_children_lock);
	if(procarray_add(&curproc->proc_children, proc, NULL)) {
		proc_destroy(proc);
		lock_release(curproc->proc_children_lock);
		return NULL;
	}
	lock_release(curproc->proc_children_lock);

	return proc;
}

void proc_clear_as(struct proc *proc) {
	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
	
	if (proc->p_addrspace) {
		struct addrspace *as;
		as_deactivate();
		as = curproc_setas(NULL);
		as_destroy(as);
	}
	
}


struct proc * proc_fork(struct proc * proc){
	struct proc *child_proc;
	if (proc == NULL) {
		return NULL;
	}
	child_proc = proc_create_runprogram(proc->p_name);
	struct addrspace * parent_as = curproc_getas();
	struct addrspace * child_as = NULL;
	if (parent_as != NULL) {
		// Copy the parent address space to the child address space
		as_copy(parent_as, &child_as);
		if (child_as == NULL) {
			return NULL;
		}
	}
	// Set the childs address space
	child_proc->p_addrspace = child_as;
	return child_proc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	t->t_proc = proc;
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			t->t_proc = NULL;
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of the current process. Caution: it isn't
 * refcounted. If you implement multithreaded processes, make sure to
 * set up a refcount scheme or some other method to make this safe.
 */
struct addrspace *
curproc_getas(void)
{
	struct addrspace *as;
#ifdef UW
        /* Until user processes are created, threads used in testing 
         * (i.e., kernel threads) have no process or address space.
         */
	if (curproc == NULL) {
		return NULL;
	}
#endif

	spinlock_acquire(&curproc->p_lock);
	as = curproc->p_addrspace;
	spinlock_release(&curproc->p_lock);
	return as;
}

/*
 * Change the address space of the current process, and return the old
 * one.
 */
struct addrspace *
curproc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
