#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <spl.h>
#include <mips/trapframe.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
void sys__exit(int exitcode, int type) {
  struct addrspace *as;
  struct proc *p = curproc;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);
  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);


  lock_acquire(p->proc_exit_lock);

 
 if(!p->proc_parent_exited && p->pid > 1){
	

	// Parent didnt exit yet, so we must only semi-destroy the proc
	proc_set_exit_status(p,exitcode, type);

	
	cv_broadcast(p->proc_exit_cv, p->proc_exit_lock);


	proc_exited_signal(p);
        /* detach this thread from its process */
        /* note: curproc cannot be used after this call */


        proc_remthread(curthread);
	// semi_destroy will release the proc_exit_lock for us.


	proc_semi_destroy(p);


 	lock_release(p->proc_exit_lock);
  }else{
	proc_exited_signal(p);
	lock_release(p->proc_exit_lock);
  	/* detach this thread from its process */
  	/* note: curproc cannot be used after this call */
	proc_remthread(curthread);
	/* if this is the last user process in the system, proc_destroy()
        will wake up the kernel menu thread */
	proc_destroy(p);
  }
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  struct proc *p = curproc;
  *retval = p->pid;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  if (status == NULL) {
	return EFAULT;
  }

  if (options != 0) {
    return(EINVAL);
  }

  struct proc * child_proc = proc_find_child(curproc, pid);
  if (child_proc == NULL) {
  	return waitpid_interested_error(pid);
  }

  lock_acquire(child_proc->proc_exit_lock);
  if(!child_proc->proc_exited){
	cv_wait(child_proc->proc_exit_cv, child_proc->proc_exit_lock);
  }

  exitstatus = child_proc->proc_exit_status;
  lock_release(child_proc->proc_exit_lock);
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

void forked_child_thread_entry(void * ptr, unsigned long val);
void forked_child_thread_entry(void * ptr, unsigned long val) {
	(void)val;
	as_activate();
	KASSERT(ptr != NULL);
	struct trapframe * tf = ptr; 
	enter_forked_process(tf);
}

int sys_fork(struct trapframe * tf, pid_t * retval){
	struct trapframe * newtf = kmalloc(sizeof(struct trapframe));
	if (newtf == NULL) {
		return ENOMEM;
	}
	*newtf = *tf;
	// disabling ALL interrupts so we can fork safely
	int spl = splhigh();
	struct proc * child_proc = proc_fork(curproc);
	if (child_proc == NULL) {
		kfree(newtf);
		splx(spl);
		return ENOMEM; 
	}
	int ret = thread_fork(curthread->t_name, child_proc, &forked_child_thread_entry,
	(void*)newtf, (unsigned long)curproc->p_addrspace);
	// can enable interrupts now
	splx(spl); 
	if (ret) {
		proc_destroy(child_proc);
		kfree(newtf);
		return ret;
	}
	*retval = child_proc->pid;
	return(0);
}
