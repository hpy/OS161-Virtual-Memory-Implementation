# OS-161 VIRTUAL MEMORY #

## Physical Memory Management: ##

### Frame table implementation #
In a paging virtual memory (vm) system, you readily need to know how
many frames of physical memory you have and which of these are free
or currently being used. To calculate the number of frames overall,
we used ram_getsize() to get the size of physical ram, then divided
it by the page size. Then we implemented our frametable as a
contiguous array of structs, one for each frame in physical memory,
each with a “used” field.

To readily access a free frame of memory, each struct had a pointer
to the next free frame in the array. If a frame was used however,
this was NULL (because it is not in the pool of free frames). A
pointer into the table at the first free frame then allowed for O(1)
allocation and deallocation by adding and removing from the head of
this list.

To initialise the frametable we used ram_getfirstfree() to calculate
the amount of frames allocated to the OS, page table, and frame table
by the bump allocator, and set their structs to be “used” and not
part of the free list.

To ensure the frametable wasn’t seen as initialized in alloc_kpages()
prior to vm_bootstrap fully completing, we used a local frame table
pointer until everything was set up, at which point we set the global
frame table pointer equal to the local one.


### Allocating frames ##

As alloc_kpages() can be called before the vm system is configured,
it needs to be able to allocate memory independant of the vm system,
but then switch over once it exists.

We checked if the vm system had been set up or not by seeing if the
frametable had been initialised (see 2 points above).

If the vm system was not initialised, we used the bump allocator
(ram_stealmem()). If it was, we used the first free frame pointer to
get the next free frame, then updated the list. If the first free
frame pointer pointed to NULL, we returned 0 as the system is
currently out of memory (OS161 handles this).

We then ensure the physical address of the base of the page is valid
and zero it out before handing it back to the caller.

To prevent concurrency issue on the frametable, we acquire a
frametable lock each time we write to the frame table and ensure we
release it whenever returning.



### Deallocating frames ##

Whilst the vm system is not set up you cannot free memory (have no
metadata about allocated memory i.e. where the alloted block starts,
its size etc.), so we return 0 in this case.

Otherwise, we change the kernel virtual address to a physical address
so that we can obtain the frame number from it. We then index the
frame table with this frame number, check it is an allocated frame,
then set it to be unused and add it as the head of the free frames
list.

Concurrency issues are dealt with via a simple frame table lock.


## Address Space Management: ##

### Defining regions #

As memory is allocated in frames, when defining a new region we had
to essentially floor the regions base address to a frame base address
by removing any offset, and then account for this by adding the
offset to the size of the region.

We then allocated a region specification struct to contain this
information, alongside the specified permissions and added it into
the current region list.

To define the stack, the only difference was we had to hard code a
region size and return the “base” of the stack. We chose 16 pages
worth of memory for the stack. We also had to set the base of the
stack to be the lower address, even though the base pointer of the
stack is at a higher address and grows down, because region bounds
checking requires the base address to be the lowest address in the
region.


### Copying regions to a new address space #

First we create a new address space, this is important as it is
required to generate the correct hash for the page table index.
We then generate an exact copy of the regions list from the old
address space and give it to the new address space (loop through
list, copying entries of each struct across individually).

Then we walk the entire page table (traversing all externally linked
chains), and if we find an entry that belongs to the old address
space and is valid, we create a new page table entry, generate a hash
index with the page virtual base address, and insert it into the page
table.

We also back the new page with a new frame, and copy across the
contents from the old page table entries backing frame to the new
page table entries backing frame. We achieve this by first converting
the frame number to a physical address, then running through the
PADDR_TO_KVADDR macro to be able to call memcpy.



### Loading regions #

In prepare load, we simply loop through all regions of an address
space and if the write bit is not set, we set it. We also turn on a
flag that lets us know the OS has modified this permission (OS_M), so
that it can be turned off once loading is complete.

In complete load, we first walk the entire page table and if an entry
belongs to the address space in question and is valid, we check to
see if the region it belongs to was modified by the OS (OS_M is set).
If it was modified we turn off the dirty bit for that entry. Once the
page table has been walked we can then turn off the write permissions
for each of the regions that also have the OS_M flag in a simple
loop.


## Address Translation: ##

### Page table implementation #

Hashed page tables generally have a power of 2 order of magnitude
number of entries as there are frames. We initialised our page table
to have twice the number of frames (calculated the same way as in the
frametable section, and allocated via the bumpallocator).
The page table is essentially an array of pointers to page table
entry structs, each containing a pid, page number, frame number and
valid bit and dirty bit in EntryLo format, as well as a pointer to
other externally linked entries.

We used a union to be able to keep the page table entry struct size
small, having an EntryLo struct represented as a single unsigned int
that can be accessed at named bit offsets.



### TLB miss handling ##
(creating page table entries, backing them with frames,
and storing an active set in the TLB)

On a VM_FAULT_READONLY, we return EFAULT as this type of fault is
assumed to just be an illegal write attempt in the basic assignment.
Otherwise, we calculate the page number that the virtual fault
address resides in to get an index into the page table of where a
valid mapping should be. We iterate the chain at this index, and if a
valid entry is found we initialise EntryHi and EntryLo variables with
the page table entry values to write a new entry into the TLB (write
random).

If a valid page table entry could not be found, we check if the
virtual fault address resides in a valid region in the address space.
If it doesn’t, we return EFAULT as this means it is an illegal
address. If it was found to be in a valid region, we create a new
page table entry, back it with a new frame and then set up the page
table entry values (set the frame number in entry low when it is
allocated, then set the dirty bit depending on whether the region is
writable or not etc.). Then we insert the new page table entry into
the page table at the correct index via the hash function, then write
into the TLB (write random).
