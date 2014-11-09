/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, unsigned long argc, char** argv)
{
	if(argc > 64){
		return E2BIG;
	}
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	vaddr_t cur_ptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

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
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}


        char* args = kmalloc(ARG_MAX);
        size_t * arg_offsets = kmalloc(argc * sizeof(size_t));
        int offset = 0;
        for(unsigned int i = 0; i < argc; i++){
                //char* arg = kmalloc(ARG_MAX);
		char* arg = argv[i];
                size_t arg_len = strlen(arg);
		if(offset + arg_len+1 > ARG_MAX){
			return E2BIG;
		}
                strcpy(args+offset,arg);
                arg_offsets[i] = offset;
                offset += ROUNDUP(arg_len+1,8);
                //argv[i] = arg;
        }

	cur_ptr = stackptr;
        cur_ptr -= offset;
	result = copyout(args, (userptr_t)cur_ptr, offset);
        if(result != 0){
                return result;
        }


        userptr_t * arg_offsets_up = kmalloc(sizeof(userptr_t) * (argc+1));
        for(unsigned int i = 0; i < argc; i++){
                userptr_t ptr = (userptr_t)cur_ptr +  arg_offsets[i];
                arg_offsets_up[i] = ptr;
        }
        arg_offsets_up[argc] = NULL;

        cur_ptr -= sizeof(userptr_t) * (argc+1);

	result = copyout(arg_offsets_up, (userptr_t)cur_ptr, sizeof(userptr_t) * (argc+1) );

	if(result != 0){
                return result;
        }


	kfree(arg_offsets);
        kfree(arg_offsets_up);
        kfree(args);

	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)cur_ptr /*userspace addr of argv*/,
			  cur_ptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

