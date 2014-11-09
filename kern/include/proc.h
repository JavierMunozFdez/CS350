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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */
#include <synch.h>
#include <limits.h>
#include <opt-A2.h>
#include <array.h>


struct addrspace;
struct vnode;
//struct fdesc;
#ifdef UW
struct semaphore;
#endif // UW
struct proc;

// Code for creating a list of processes
#ifndef PROCINLINE
#define PROCINLINE INLINE
#endif

DECLARRAY(proc);
DEFARRAY(proc, PROCINLINE);



/*
 * Process structure.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	struct threadarray p_threads;	/* Threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */
	
	const pid_t pid; /* the ID of this process */

	// The exit status of this processor
	volatile int proc_exit_status;
	// Predicate to determine if this process has already exited
	volatile bool proc_exited;
	// Predicate to determine if the parent of this process has exited
	volatile bool proc_parent_exited;
	// lock used for the children array of this process for mutex
	struct lock * proc_children_lock;
	// lock and cv for when the process waits for child to exit (waitpid)
	struct lock * proc_exit_lock;
	struct cv * proc_exit_cv;
	// List of chilren
	struct procarray proc_children;
	
		
		
#ifdef UW
  /* a vnode to refer to the console device */
  /* this is a quick-and-dirty way to get console writes working */
  /* you will probably need to change this when implementing file-related
     system calls, since each process will need to keep track of all files
     it has opened, not just the console. */
  struct vnode *console;                /* a vnode for the console device */
#endif

	/* add more material here as needed */
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

// bitmap used for process ids
struct bitmap * proc_id_map;
// spinlock for the bitmap
struct spinlock proc_id_map_spinlock;

void proc_clear_as(struct proc *proc);

// Set the exit status of the proc to exitcode
void proc_set_exit_status ( struct proc *proc, const int exitcode, const int type);

// Finds any child of the process proc that has the id pid
struct proc * proc_find_child(struct proc * proc, const int pid);

// Notifies the children of the process proc that their parent has exited
void proc_exited_signal ( struct proc * proc);

// Handles the situation where the process we are wiating for 
// is not interested in the calling process
int waitpid_interested_error(const int pid);

// Semi destroys the process
void proc_semi_destroy(struct proc *proc);


/* Semaphore used to signal when there are no more processes */
#ifdef UW
extern struct semaphore *no_proc_sem;
#endif // UW

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

// Creates a forked process of the given process proc
struct proc *proc_fork(struct proc * proc);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *curproc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *curproc_setas(struct addrspace *);

// Assigns the next available pid to the ptr that points to the pid field of the proc
int proc_assign_pid(int * proc_pid_addr_ptr);

// When a process exits, and no one is interested in the proccess anymore, sets the pid
// to be reused.
void proc_set_pid_unused(const int pid);



#endif /* _PROC_H_ */
