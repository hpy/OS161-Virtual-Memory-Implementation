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


static void copyframe(struct pagetable_entry *from, struct pagetable_entry *to);
struct pagetable_entry *destroy_page(struct addrspace *as,struct pagetable_entry *entry);




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


static int
as_copy_region(struct addrspace *as, struct region_spec *region){
        int r = region->as_perms & PF_R;
        int w = region->as_perms & PF_W;
        int x = region->as_perms & PF_X;
        return as_define_region(as, region->as_vbase , region->as_npages * PAGE_SIZE, r, w, x);
}




int copy_page_table(struct addrspace *old, struct addrspace *new);


int
copy_page_table(struct addrspace *old, struct addrspace *new){

    int npages = pagespace / sizeof(struct pagetable_entry *);
    //kprintf("COPYPPPPPPPPPPPPPPPP: %d\n",npages);
    for(int i = 0; i<npages; i++){
        struct pagetable_entry * curr = pagetable[i];

        while(curr!=NULL){
            /* copy pages from old address space */
            if(curr->pid == old){
                vaddr_t faultframe = (curr->pagenumber)<<FRAME_TO_PADDR;
                uint32_t index = hpt_hash(new, faultframe);

                //assert frame matches
                KASSERT(hpt_hash(old, faultframe) == (uint32_t)i);

                /* create a new page table entry  */
                struct pagetable_entry *page_entry = create_page(new,curr->pagenumber,curr->entrylo.lo.dirty);
                if(page_entry==NULL){
                    return ENOMEM;
                }

                /* insert new page table entry */
                insert_page(index,page_entry);

                copyframe(curr, page_entry);

            }
            curr = curr->next;
        }
    }
    return 0;
}


int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    *ret = NULL;
    int result = 0;

    if(old==NULL){
        return 0;
    }

    struct addrspace *new_as = as_create();
    if (new_as==NULL) {
            return ENOMEM;
    }

    //do we need a lock on the address space?
    struct region_spec *curr_region = old->regions;
    while(curr_region!=NULL){
        /* copy old regions into the new address space*/
        result = as_copy_region(new_as,curr_region);
        if(result){
            as_destroy(new_as);
            return result;
        }
        curr_region = curr_region->as_next;
    }

    /* allocate new page table entries*/
    result = copy_page_table(old, new_as);
    if(result){
        as_destroy(new_as);
        return result;
    }

    *ret = new_as;
    return 0;
}


/*
    copy frame from a to b
*/
static void
copyframe(struct pagetable_entry *from, struct pagetable_entry *to){
    int from_frame = from->entrylo.lo.framenum;
    paddr_t from_paddr = from_frame<<FRAME_TO_PADDR;

    int to_frame = to->entrylo.lo.framenum;
    paddr_t to_paddr = to_frame<<FRAME_TO_PADDR;

    to_paddr = PADDR_TO_KVADDR(to_paddr);
    from_paddr = PADDR_TO_KVADDR(from_paddr);

    memcpy((void *)to_paddr,(void *)from_paddr,PAGE_SIZE);
}




void
as_destroy(struct addrspace *as)
{

    /* free all pages and frames */

    int npages = pagespace / sizeof(struct pagetable_entry);

    for (int i = 0; i < npages; i++) {
        struct pagetable_entry *curr_page = pagetable[i];
        struct pagetable_entry *next_page = NULL;
        while(curr_page!=NULL){
                next_page = curr_page->next;
                /* free each page and frame */
                if(curr_page->pid == as){
                    paddr_t framenum = (curr_page->entrylo.lo.framenum)<<FRAME_TO_PADDR;
                    free_kpages(PADDR_TO_KVADDR(framenum));
                    if(curr_page->pid!=0){
                        //panic("ERROR\n");
                    }
                }
                curr_page = next_page;
        }
    }

    /* free all regions */
    struct region_spec *curr_region = as->regions;
    struct region_spec *next_region = NULL;
    while(curr_region!=NULL){
        next_region = curr_region->as_next;
        kfree(curr_region);
        curr_region = next_region;
    }

    kfree(as);

    /* Flush TLB */
    as_activate();

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

        size_t npages;

        /* Align the region. First, the base... */
        memsize += vaddr & ~(vaddr_t)PAGE_FRAME; //add offset onto memsize
        vaddr &= PAGE_FRAME; //chop the offset off the virtual address

        /* ...and now the length. */
        memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME; //ceiling memsize to nearest pagesize

        npages = memsize / PAGE_SIZE;
        KASSERT(npages > 0);
        struct region_spec *region = kmalloc(sizeof(struct region_spec));
        if(region==NULL){
            return ENOMEM;
        }

        if(readable) region->as_perms |= PF_R;
        if(writeable) region->as_perms |= PF_W;
        if(executable) region->as_perms |= PF_X;

        region->as_vbase = vaddr;
        region->as_npages = npages;
        region->as_next = as->regions;
        as->regions = region;

        //kprintf("NEW REGIONS: %x -> %x\n",vaddr,vaddr + (npages*PAGE_SIZE));

        return 0;
}

int
as_prepare_load(struct addrspace *as)
{
        struct region_spec *curr_region = as->regions;

        //set all regions as read write -> temp until extended assignment
        while(curr_region!=NULL){
            if(!(curr_region->as_perms & PF_W)){
                //modified flag so that we can revert the write flag after load
                curr_region->as_perms |= (PF_W | OS_M );
            }
            curr_region = curr_region->as_next;
        }
        return 0;
}


/*
- Re-set the permissions in the addrspace struct to what they were before

- Go through the page table and unset the (D)irty bits on the page table entries for any pages that were loaded into (now) read-only regions

- You also need to remove these pages from the TLB (you can just flush the whole TLB).
*/
int
as_complete_load(struct addrspace *as)
{
    struct region_spec *curr_region = as->regions;
    while(curr_region!=NULL){
        /* remove modified and writeable flags from modified regions */
        if(curr_region->as_perms & OS_M){
            curr_region->as_perms &= ~(PF_W | OS_M );

            // https://piazza.com/class/jdwg14qxhhb4kp?cid=536

            /* Look up page frames in this region*/
            vaddr_t frame = curr_region->as_vbase & PAGE_FRAME;
            uint32_t index = hpt_hash(as, frame);
            struct pagetable_entry *hpt_entry = pagetable[index];

            /* Set all entries dirty bit to read only */
            while(hpt_entry!=NULL){
                hpt_entry->entrylo.lo.dirty = 0;
                hpt_entry = hpt_entry->next;
            }

        }
        curr_region = curr_region->as_next;
    }

    /* Flush TLB */
    as_activate();

    return 0;
}



int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    vaddr_t stackbase = USERSTACK - STACKSIZE;

    int result = as_define_region(as, stackbase, STACKSIZE, VALID_BIT, VALID_BIT, INVALID_BIT);
    if(result){
        return result;
    }

    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    return 0;
}










 /* You must hold pagetable lock before calling this function */
 struct pagetable_entry *
 destroy_page(struct addrspace *as,struct pagetable_entry *entry){
     if(entry==NULL){
         return NULL;
     }

     if(entry->pid == as){
         struct pagetable_entry *next = entry->next;
         kfree(entry);
         return next;
     }

     entry->next =destroy_page(as,entry->next);
     return entry;
 }
