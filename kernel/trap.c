#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  }
  #if(defined(NFUA) || defined(LAPA) || defined(SCFIFO))
  //page fault
  else if(r_scause() == 13 || r_scause() == 15 || r_scause() == 12){
    uint64 va = PGROUNDDOWN(r_stval());
    printf("User pagefault at %p\n", va);
    int found_page_address = 0;
    for(int i = 0; i < MAX_TOTAL_PAGES; i++){
      if(p->p_pages[i].v_address == va){
        if(p->in_ram_count == MAX_PYSC_PAGES){
          int free_offset = get_free_swapfile_offset();
          if(free_offset != -1)
            swap_out(free_offset);
          else
            panic("Swapfile is full");
        }
        pte_t *pte = walk(p->pagetable,va, 0);
        if(*pte & PTE_PG){
          char * p_address = kalloc();
          memset(p_address, 0, PGSIZE);
          p->p_pages[i].v_address = va;
          p->p_pages[i].in_ram = 1; 
          p->p_pages[i].allocated = 1;
          #if (defined(LAPA))
          p->p_pages[i].age = 0xFFFFFFFF;
          #else
          p->p_pages[i].age = 0;
          #endif
          p->sc_fifo_queue[p->in_ram_count] = i;
          p->in_ram_count++;
          p->total_pages_in_swapfile--;
          // sfence_vma();
          if(mappages(p->pagetable, p->p_pages[i].v_address, PGSIZE, (uint64)p_address, PTE_W|PTE_X|PTE_R|PTE_U) < 0) {
            printf("mapping out of memory\n");
            kfree(p_address);
            return;
          }
          readFromSwapFile(p, p_address, p->p_pages[i].swapfile_offset,PGSIZE);
          *pte |= PTE_V; // turning on valid flag
          *pte &= ~(PTE_PG); // turning off secondary storage flag
           p->swapFile_offset[(int)(p->p_pages[i].swapfile_offset/PGSIZE)] = 1;
           p->p_pages[i].swapfile_offset = -1;

        }
        else {
          printf("v address %p, in ram ? %d\n",p->p_pages[i].v_address, p->p_pages[i].in_ram);
          panic("Page does not exists");
        }

        found_page_address = 1;


        // pte_t *pte = walk(p->pagetable, p->p_pages[i].v_address, p->p_pages[i].allocated);
        // *pte &= ~(PTE_PG); // turning off secondary storage flag
        // *pte |= PTE_V; // turning on valid flag
        break;
      }
    }
    if(!found_page_address){
        printf("va %p\n", va);
        panic("Illegal address in pagefault");
    }
  }
  #endif
  else if((which_dev = devintr()) != 0){
    // ok
  }
   else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){              
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;

  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

