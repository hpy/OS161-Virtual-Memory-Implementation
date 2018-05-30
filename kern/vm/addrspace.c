/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *        The President and Fellows of Harvard College.
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
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
        struct addrspace *as;
        as = kmalloc(sizeof(struct addrspace));
        if (as == NULL) {
                return NULL;
        }
        as->regions = NULL;
        return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    *ret = NULL;

    if(old==NULL){
        return 0;
    }

    struct addrspace *newas;
    newas = as_create();
    if (newas==NULL) {
            return ENOMEM;
    }

    struct region_spec *curr = old->regions;
    /* copy all regions from old into new */
    while(curr!=NULL){
        int result = as_define_region(newas, curr->as_vbase , curr->as_regsize, curr->as_perms, 0, 0);
        if(result){
            as_destroy(newas);
            return result;
        }
        curr = curr->as_next;
    }

    *ret = newas;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
        /*
         * Clean up as needed.
         */

        kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

    /* Flush TLB */
	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
        /* Flush TLB */
        as_activate();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                 int readable, int writeable, int executable)
{
        if(as==NULL){
            return 0; //fix this error code
        }

        struct region_spec *region = kmalloc(sizeof(struct region_spec));
        if(region==NULL){
            return ENOMEM;
        }

        region->as_perms = 0 | (readable | writeable | executable);
        region->as_vbase = vaddr;
        region->as_regsize = memsize;
        region->as_next = as->regions;
        as->regions = region;

        return 0;
}

int
as_prepare_load(struct addrspace *as)
{
        struct region_spec *curr = as->regions;

        //set all regions as read write -> temp until extended assignment
        while(curr!=NULL){
            if(!(curr->as_perms & PF_W)){
                //modified flag so that we can revert the wrtie flag after load
                curr->as_perms |= (PF_W | OS_M );
            }
            curr = curr->as_next;
        }
        return 0;
}

int
as_complete_load(struct addrspace *as)
{
    struct region_spec *curr = as->regions;
    //set all regions as read write -> temp until extended assignment
    while(curr!=NULL){
        if(curr->as_perms & OS_M){
            //remove modified and writeable flags
            curr->as_perms &= ~(PF_W | OS_M );
        }
        curr = curr->as_next;
    }
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    size_t stacksize = FIXED_STACK_SIZE * PAGE_SIZE;
    vaddr_t stackbase = USERSTACK - stacksize;
    int result = as_define_region(as, stackbase, stacksize,PF_R, PF_W, 0);

    if(result){
        return result;
    }

    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    return 0;
}
