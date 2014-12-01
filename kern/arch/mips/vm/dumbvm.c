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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>
#include <spinlock.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
volatile int vm_bootstrap_complete = 0;
paddr_t start_physical_mem = 0;
paddr_t end_physical_mem = 0;
volatile int page_frames = 0;

void
vm_bootstrap(void)
{
	//get the ram size after all the kernel-specific structs ahve been added to ram
	ram_getsize( &start_physical_mem,  &end_physical_mem);
	spinlock_init(&coremap_spinlock);
	//spinlock_init(&start_physical_mem_lk);
	spinlock_acquire(&coremap_spinlock);
	//spinlock_acquire(&start_physical_mem_lk);
	//allocate space for coremap
	//coremap = (struct coremap_frame*)PADDR_TO_KVADDR(start_physical_mem);
	// get the number of page framez that we can have in the remaining ram
	page_frames = ((end_physical_mem - start_physical_mem) / PAGE_SIZE); 
	coremap = (int*)PADDR_TO_KVADDR(start_physical_mem);
	for(int i = 0; i < page_frames; i++){
		coremap[i] = 0;
	}
	start_physical_mem = ROUNDUP(start_physical_mem,PAGE_SIZE);
	// offse tthe start of physical mem by adding space for coremap
	start_physical_mem += sizeof(int) * page_frames;
	// round to page size
	start_physical_mem = ROUNDUP(start_physical_mem,PAGE_SIZE);
	//spinlock_release(&start_physical_mem_lk);
	// vm bootstrap flag complete
	spinlock_release(&coremap_spinlock);
	vm_bootstrap_complete = 1;
	/* Do nothing. */
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
	long s_npages = npages;
	if( vm_bootstrap_complete == 0){
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		return addr;
	}else{
		addr = 0;
		spinlock_acquire(&coremap_spinlock);
		long max_so_far = 0;
		int i;
		for(i = 0; i < page_frames; i++){
			if(coremap[i] != 0){
				max_so_far = 0;
				continue;
			}else{
				max_so_far++;
				if(max_so_far == s_npages){
					break;
				}
			}
		}
		if(max_so_far != 0){
			addr = start_physical_mem + (PAGE_SIZE * i);
			for(int j = 0; j < s_npages; j++){
				coremap[i+j] = npages;
			}
		}
		spinlock_release(&coremap_spinlock);
		return addr;
	}
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{

	paddr_t pa;
	pa = getppages(npages);

	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */
	paddr_t p_addr = KVADDR_TO_PADDR(addr);
	long index = (p_addr-start_physical_mem)/PAGE_SIZE;
	spinlock_acquire(&coremap_spinlock);
	long npages = coremap[index];
                        for(long j = 0; j < npages; j++){
                                coremap[index+j] = 0;
                        }
	spinlock_release(&coremap_spinlock); 
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}


bool address_in_segment(vaddr_t top, vaddr_t bottom, vaddr_t addr);
bool address_in_segment(vaddr_t top, vaddr_t bottom, vaddr_t addr){
	return addr >= bottom && addr < top;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
                 //if(address_in_segment(vtop1,vbase1,faultaddress)){
			 // check to see if it
                        //address is in text segment and we are trying to write to it but
                        // we should not be able to write to this address
      		return EFAULT;
                //}
	    case VM_FAULT_READ:
		//Tried to read a tlb entry
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	int page;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		page = (faultaddress - vbase1)/PAGE_SIZE;
		paddr = as->as_pbase1[page];
//		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		page = (faultaddress - vbase2)/PAGE_SIZE;
		paddr = as->as_pbase2[page];
		//paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		page = (faultaddress - stackbase)/PAGE_SIZE;
		paddr = as->as_stackpbase[page];
//		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
                bool vaddr_in_text_segment = address_in_segment(vtop1,vbase1,faultaddress);
                if(vaddr_in_text_segment){
			if(as->done_loading == true){
				elo = paddr | TLBLO_VALID;
			}else{
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
			}
                }else{
			elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
                }
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	//We have read all the TLB entries and all of them seem to be used
	//Now we must get rid of a random tlb entry using tlb_random
		ehi = faultaddress;
		bool vaddr_in_text_segment = address_in_segment(vtop1,vbase1,faultaddress);
                if(vaddr_in_text_segment){
                        if(as->done_loading == true){
                                elo = paddr | TLBLO_VALID;
                        }else{
                                elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
                        }
                }else{
                        elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
                }
                DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
                tlb_random(ehi, elo);
                splx(spl);
                return 0;

}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
	as->done_loading = false;
	as->as_vbase1 = 0;
	as->as_pbase1 = (paddr_t*)NULL;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = (paddr_t*)NULL;
	as->as_npages2 = 0;
	as->as_stackpbase = (paddr_t*)NULL;

	return as;
}

void
as_destroy(struct addrspace *as)
{
        for(unsigned int i = 0; i < as->as_npages1; i++){
        	free_kpages(PADDR_TO_KVADDR(as->as_pbase1[i]));
	}

        for(unsigned int j = 0; j < as->as_npages2; j++){
                free_kpages(PADDR_TO_KVADDR(as->as_pbase2[j]));
        }

        for(int k = 0; k < DUMBVM_STACKPAGES; k++){
                free_kpages(PADDR_TO_KVADDR(as->as_stackpbase[k]));
	}
	kfree(as->as_pbase1);
	kfree(as->as_pbase2);
	kfree(as->as_stackpbase);
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->as_pbase1 = kmalloc(sizeof(paddr_t) * npages);
		for(unsigned int i = 0; i < npages; i++){
			as->as_pbase1[i] = (paddr_t)NULL;
		}
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_pbase2 = kmalloc(sizeof(paddr_t) * npages);
		for(unsigned int i = 0; i < npages; i++){
                        as->as_pbase2[i] = (paddr_t)NULL;
                }
		return 0;
	}



	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	//KASSERT(as->as_stackpbase == NULL);

//	kprintf("a\n");

	for(unsigned int i = 0; i < as->as_npages1; i++){
		as->as_pbase1[i] = getppages(1);
		if (as->as_pbase1[i] == (paddr_t)NULL) {
			return ENOMEM;
		}
	}

//	kprintf("b\n");

	for(unsigned int m = 0; m < as->as_npages1; m++){
		as_zero_region(as->as_pbase1[m], 1);
	}
	//as->as_pbase1 = getppages(as->as_npages1);

//kprintf("c\n");

	for(unsigned int j = 0; j < as->as_npages2; j++){
		as->as_pbase2[j] = getppages(1);
        	if (as->as_pbase2[j] == (paddr_t)NULL) {
                	return ENOMEM;
        	}
	}


//kprintf("d\n");

        for(unsigned int n = 0; n < as->as_npages2; n++){
                as_zero_region(as->as_pbase2[n], 1);
        }
	//as->as_pbase2 = getppages(as->as_npages2);
	//if (as->as_pbase2 == 0) {
	//	return ENOMEM;
	//}


	//as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	//if (as->as_stackpbase == 0) {
	//	return ENOMEM;
	//}

	//as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
//  	kprintf("a\n");
        as->as_stackpbase = kmalloc(sizeof(paddr_t) * DUMBVM_STACKPAGES);
        for(int i = 0; i < DUMBVM_STACKPAGES; i++){
        	as->as_stackpbase[i] = (paddr_t)NULL;
        }



        for(int j = 0; j < DUMBVM_STACKPAGES; j++){
                as->as_stackpbase[j] = getppages(1);
                if (as->as_stackpbase[j] == (paddr_t)NULL) {
                        return ENOMEM;
                }
        }

	for(int k = 0; k < DUMBVM_STACKPAGES; k++){
		 as_zero_region(as->as_stackpbase[k], 1);
	}

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{

//	kprintf("a\n");
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

//	kprintf("b\n");


	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
	new->as_pbase1 = kmalloc(sizeof(paddr_t) * old->as_npages1);
	new->as_pbase2 = kmalloc(sizeof(paddr_t) * old->as_npages2);
	new->as_stackpbase = kmalloc(sizeof(paddr_t) * DUMBVM_STACKPAGES);
	new->done_loading = false;

//	kprintf("c\n");

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != NULL);
	KASSERT(new->as_pbase2 != NULL);
	KASSERT(new->as_stackpbase != NULL);

        for(int i = 0; i < DUMBVM_STACKPAGES; i++){
                new->as_stackpbase[i] = (paddr_t)NULL;
        }

        for(int j = 0; j < DUMBVM_STACKPAGES; j++){
                new->as_stackpbase[j] = getppages(1);
                if (new->as_stackpbase[j] == (paddr_t)NULL) {
                        return ENOMEM;
                }
        }

        for(int k = 0; k < DUMBVM_STACKPAGES; k++){
                 as_zero_region(new->as_stackpbase[k], 1);
        }


//kprintf("d\n");

	for(unsigned int i = 0; i < old->as_npages1; i++){
		memmove(
			(void*)PADDR_TO_KVADDR(new->as_pbase1[i]),
			(const void*)PADDR_TO_KVADDR(old->as_pbase1[i]),
			(size_t)PAGE_SIZE);
	}

//kprintf("e\n");
        for(unsigned int j = 0; j < old->as_npages2; j++){
                memmove(
                        (void*)PADDR_TO_KVADDR(new->as_pbase2[j]),
                        (const void*)PADDR_TO_KVADDR(old->as_pbase2[j]),
                        (size_t)PAGE_SIZE);
        }

//kprintf("f\n");
        for(int k = 0; k < DUMBVM_STACKPAGES; k++){
                memmove(
                        (void*)PADDR_TO_KVADDR(new->as_stackpbase[k]),
                        (const void*)PADDR_TO_KVADDR(old->as_stackpbase[k]),
                        (size_t)PAGE_SIZE);
        }
	*ret = new;
	return 0;
}
