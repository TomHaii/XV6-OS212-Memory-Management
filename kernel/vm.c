#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

int get_free_swapfile_offset();
// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V){
      panic("remap");
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  struct proc* p = myproc();
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  int i = -1;
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    for(i = 0; i < MAX_TOTAL_PAGES; i++){
      if(p->p_pages[i].v_address == a){
        break;
      }
    }
    // if(i == -1)
    //   panic("uvmunmap: no such VA");
    
    if((pte = walk(pagetable, a, 0)) == 0){
      #ifdef NONE
      return;
      #endif
      panic("uvmunmap: walk");
    
    }
    if(((*pte & PTE_V) == 0) && ((*pte & PTE_PG) == 0)){
      #ifndef NONE
      panic("uvmunmap: not mapped");
      #endif
    }
    #ifdef NONE
    if(do_free && (*pte & PTE_V)){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    #endif
    #ifndef NONE
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(!(*pte & PTE_PG)){
      if(do_free){
       if(p->pid > 2 && p->pagetable == pagetable){
        p->p_pages[i].v_address = -1;
        int j;
        for(j = 0; j < MAX_PYSC_PAGES; j++){
          if(p->sc_fifo_queue[j] == i)
            break;
        }
        for(; j< MAX_PYSC_PAGES-1; j++){
          p->sc_fifo_queue[j] = p->sc_fifo_queue[j+1];
        }
        p->p_pages[i].in_ram = 0;
        p->p_pages[i].allocated = 0;
        p->p_pages[i].age = 0;
        p->p_pages[i].sc_used = 0;
        p->in_ram_count--;
        *pte &= ~(PTE_V); // turning off valid flag
        }
        uint64 pa = PTE2PA(*pte);
        kfree((void*)pa);
      }
    }
    else if(*pte & PTE_PG && (!(*pte & PTE_V))){
      if(p->pagetable == pagetable){
      *pte &= ~(PTE_PG);
      p->p_pages[i].v_address = -1;
      p->p_pages[i].allocated = 0;
      p->p_pages[i].swapfile_offset = -1;
      p->swapFile_offset[(int)(p->p_pages[i].swapfile_offset / PGSIZE)] = 1;
      p->total_pages_in_swapfile++;
      }
    }
    #endif
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  struct proc* p = myproc();
  char *mem;
  uint64 a;
  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){

    #ifndef NONE
    //check if we exceeded the max page size
     if((p->pid > 2) && (p->in_ram_count >= MAX_PYSC_PAGES) && p->pagetable == pagetable){
     // if((p->pid > 2) && (a >= PGSIZE*MAX_PYSC_PAGES)){
        int free_offset = get_free_swapfile_offset();
        if(free_offset == -1){
          panic("Exceeded maximum pages");
        }
        p->total_pages_in_swapfile++;
        swap_out(free_offset);
      }
    #endif
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    #ifndef NONE
    if(p->pid > 2 && p->pagetable == pagetable)
    {
      int page_index = 0;
      //We didn't find a page in ram with the same virtual address as a
        while((page_index < MAX_TOTAL_PAGES) && (p->p_pages[page_index].v_address != -1)){
           page_index++;
        }
      //We didn't find a page in ram with the same virtual address as a
      // while((page_index < MAX_TOTAL_PAGES) && (p->p_pages[page_index].v_address != -1)){
      //     page_index++;
      // }

      if(page_index >= MAX_TOTAL_PAGES){
        for(int i = 0; i < MAX_TOTAL_PAGES; i++){
          printf("Index %d, VA %p, IN RAM? %d\n", i,p->p_pages[i].v_address, p->p_pages[i].in_ram);
        }
        panic("Exceeded number of total pages");
      }
      p->p_pages[page_index].v_address = PGROUNDDOWN(a);
      p->p_pages[page_index].in_ram = 1; 
      p->p_pages[page_index].allocated = 1;
      #ifdef LAPA
      p->p_pages[page_index].age = 0xFFFFFFFF;
      #else
      p->p_pages[page_index].age = 0;
      #endif
      p->p_pages[page_index].swapfile_offset = -1;
      p->sc_fifo_queue[p->in_ram_count] = page_index;
      p->in_ram_count++;
     // printf("initalizing va %p at index %d\n", p->p_pages[page_index].v_address, page_index);
      pte_t *pte = walk(p->pagetable, p->p_pages[page_index].v_address, p->p_pages[page_index].allocated);
      *pte &= ~(PTE_PG); // turning off secondary storage flag
      *pte |= PTE_V; // turning on valid flag
    }
    #endif
  }
  #ifdef NONE
  p->lazy_sz = newsz;
  #endif
  return newsz;
}


uint select_page(){
  //struct proc *p = myproc();
  uint selected_page = 0;
  #if (defined(NFUA))
    selected_page = select_page_NFUA();
  #endif
  #if (defined(LAPA))
   selected_page = select_page_LAPA();
  #endif
  #if (defined(SCFIFO))
  selected_page = select_page_SCFIFO();
  #endif
  return selected_page;
}

int get_free_swapfile_offset(){
  struct proc *p = myproc(); 

  for(int i = 0 ; i < MAX_PYSC_PAGES; i++){
    if(p->swapFile_offset[i]){
      return (i*PGSIZE);
    }
  }

  return -1;
}


uint64 swap_out(uint offset){
  struct proc* p = myproc();
 // pte_t *pte;
  uint64 address_to_write_from = select_page();
  address_to_write_from = PGROUNDDOWN(address_to_write_from);
  pte_t *ptet = walk(p->pagetable, address_to_write_from, 0);
  char* pa = (char*)PTE2PA(*ptet);
 // printf("pid %d, start write to swap file - offset %d address - %p , physical %p\n",p->pid,offset, address_to_write_from, pa);
  writeToSwapFile(p, pa, offset, PGSIZE); // write the page in the free space in the swap file
  int page_index = 0;
  // get the page address we want to swap
  while(page_index < MAX_TOTAL_PAGES && p->p_pages[page_index].v_address != address_to_write_from){
    page_index++;
  }
  for(int j=1; j< MAX_PYSC_PAGES ; j++){
    p->sc_fifo_queue[j-1] = p->sc_fifo_queue[j];
  }
  p->p_pages[page_index].swapfile_offset = offset;
  p->swapFile_offset[(int)(offset/PGSIZE)] = 0;
  p->p_pages[page_index].in_ram = 0;
  p->p_pages[page_index].allocated = 0;
  p->in_ram_count--;
  
  
  *ptet |= PTE_PG; // turning on secondary storage flag
  *ptet &= ~(PTE_V); // turning off valid flag
  kfree((void*)(pa));
  sfence_vma();
  return address_to_write_from;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  
  // #ifdef NONE
  // struct proc* p = myproc();
  // #endif
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }
  // #ifdef NONE
  // p->lazy_sz = newsz;
  // #endif
  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      #ifndef NONE
      panic("freewalk: leaf");
      #endif
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0){
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
    }
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  
  pte_t *pte;
  #ifndef NONE
  pte_t *np_pte;
  #endif
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0){
      #ifndef NONE
      panic("uvmcopy: pte should exist");
      #endif
      return 0;
    }
    if(!(*pte & PTE_V) && !(*pte && PTE_PG)){
      #ifndef NONE
      panic("uvmcopy: page not present and not on disk");
      #endif
      return 0;
    }  
    #ifndef NONE
    if(*pte & PTE_PG){  // paged swapped out be we still want to copy entry {
      if((np_pte = walk(new, i, 1)) == 0)
        return -1;
    *np_pte = *pte;
    continue;
    }
    #endif
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

  err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
