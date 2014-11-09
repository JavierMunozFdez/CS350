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
#include <limits.h>
#include <vm.h>
#include <vfs.h>
#include <kern/fcntl.h>

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


int sys_execv(userptr_t progname_ptr, userptr_t args_ptr){

	
	char* prog_name = kmalloc(PATH_MAX); 
	size_t prog_name_len;
	if((char*)progname_ptr == NULL || (char**)args_ptr == NULL){
		return EFAULT;
	}
	int argc = 0;
	char** args_ptrs = kmalloc(ARG_MAX);
        while(1){
                char* arg = NULL;
                copyin(args_ptr+argc*sizeof(char*), &arg, sizeof(char*));
		args_ptrs[argc] = arg;
		if(arg != NULL){
			argc++;
		}else{
			break;
		}
	}
	if(argc> 64){
		return E2BIG;
	}

	int result_prog_name = copyinstr(progname_ptr,prog_name,PATH_MAX, &prog_name_len);
	if(result_prog_name != 0){
		return result_prog_name;
	}


	char* argv = kmalloc(ARG_MAX);
	size_t * arg_offsets = kmalloc(argc * sizeof(size_t));
        int offset = 0;
	for(int i = 0; i < argc; i++){
		//char* arg = kmalloc(ARG_MAX);
		size_t arg_len;
		int result = copyinstr((userptr_t)args_ptrs[i],
				 argv+offset, ARG_MAX - offset, &arg_len );
		if(result != 0){
			return result;
		}
		arg_offsets[i] = offset;
		offset += ROUNDUP(arg_len+1,8);
		//argv[i] = arg;
	}

	// prog_name (char*)   AND   argv (char*)

	struct addrspace *curproc_as = curproc_getas();
	

	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	vaddr_t cur_ptr;
	int result;
	/* Open the file. */
	result = vfs_open(prog_name, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}
	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();
	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}
	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);

	// Have both argc and prog_name ready for runprogram
	cur_ptr = stackptr;
	cur_ptr -= offset;
	result = copyout(argv, (userptr_t)cur_ptr, offset);
	if(result != 0){
		return result;
	}

	userptr_t * arg_offsets_up = kmalloc(sizeof(userptr_t) * (argc+1));
	for(int i = 0; i < argc; i++){
		userptr_t ptr = (userptr_t)cur_ptr +  arg_offsets[i];
		arg_offsets_up[i] = ptr;
	}
	arg_offsets_up[argc] = NULL;

	cur_ptr -= sizeof(userptr_t) * (argc+1);

	result = copyout(arg_offsets_up, (userptr_t)cur_ptr, sizeof(userptr_t) * (argc+1) );

	if(result != 0){
		return result;
	}

	as_destroy(curproc_as);
	kfree(arg_offsets);
	kfree(arg_offsets_up);
	kfree(argv);
	kfree(args_ptrs);
	kfree(prog_name);

	enter_new_process(argc /*argc*/, (userptr_t)cur_ptr /*userspace addr of argv*/,
	cur_ptr, entrypoint);
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;

	//return 0;
}
