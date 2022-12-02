#include "userprog/exception.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "vm/spt.h"
#include "vm/mmap.h"
#include "devices/swap.h"
#include "lib/string.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "vm/sharing.h"
#include "vm/frame.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
static bool actual_load_page(struct spt_entry *spe);
static uint8_t *
get_and_install_page(enum palloc_flags flags, 
                     void *upage, 
                     uint32_t *pagedir, 
                     bool writable,
                     bool is_filesys,
                     char *name,
                     unsigned int page_num);
static bool 
actual_load_mmap_page(struct page_mmap_entry *pentry);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */

  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      delete_thread(-1);
      PANIC ("Could not delete thread in SEL_UCSEC");

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  
         Shouldn't happen.  Panic the kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      PANIC ("Kernel bug - this shouldn't be possible!");
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to task 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{ 
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */
  
  void *esp = NULL;     /* User Stack Pointer */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause useful for debugging */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0; 

  struct thread *t = thread_current();

  /* setting user stack pointer and validating addr if user faulted */
  if (user)
  {
    esp = f->esp;
    if (!is_user_vaddr(fault_addr))
    {  
      goto failure;
    }
  } 
  else 
  {
    if (thread_current()->in_sys_call)
    {
      esp = thread_current()->stack_pt;
    }
  }

  /* check SPT if page was not present */
  if (not_present)
   {
      struct hash spt = t->sp_table;
      struct spt_entry fake_sptentry;
      void *fault_upage = pg_round_down(fault_addr);
      fake_sptentry.upage = fault_upage;

      // printf("EIP: %p\n", f->eip);
      // printf("FAULTING UPAGE: %p  fault_addr:%p\n", fake_sptentry.upage, fault_addr);

      struct hash_elem *found = hash_find (&spt, &fake_sptentry.elem);

      /* Use SPT data to handle page fault */
      if (found)
      {
         struct spt_entry *spe = hash_entry(found, struct spt_entry, elem);
         if (!spe->writable && write)
         {
            // user tried to write to a read only page
            goto failure;
         }

         /* Code reaching here indicates that access was valid, load neccesary */ 
         if (spe->location == FILE_SYS || spe->location == ALL_ZERO)
         {
               if (!actual_load_page(spe))
               {  
               printf("Failed to load spt page entry at addr: %p\n", fault_addr);
               goto failure;
               }
         } 
         else
         {
               /* Page must exist in a swap slot */
               ASSERT (spe->location == SWAP_SLOT);
               // printf("SWAPSWAP \n");
               // TODO : does this install also??? WHat about required evictions
               void *kpage = palloc_get_page(PAL_USER);
               ASSERT(kpage);
               // printf("SWAP_IN:%d \n", spe->swap_slot);
               swap_in (kpage, spe->swap_slot); 
               if (!install_page(spe->upage, kpage, spe->writable))
               {
                  printf("Could not install swapped in page \n");
                  palloc_free_page(kpage);
                  goto failure;
               }
               spe->location = spe->location_prev;
         }
         return;
      }


      /* Page not found in supplemental page table.
         Now checking whether page corresponds to memory mapped file */
      struct page_mmap_entry *pentry 
         = get_mmap_page(&t->page_mmap_table, fault_upage);
      if (pentry)
      {
         if (!actual_load_mmap_page(pentry))
         {
            NOT_REACHED();
         }
         return;
      }

      /* Checking for stack fault*/
      if(esp)
      {
        if ((fault_addr >= esp) 
           || (fault_addr == esp - 32) 
           || (fault_addr == esp - 4))
        {
           /* Checking for overflow of stack pages */
           void *next_upage = pg_round_down(fault_addr);

           // TODO Check with Mark why this is working
           if (next_upage - PHYS_BASE > STACK_MAX_SIZE)
           {
              goto failure;
           }
           uint8_t *k_new_page = get_and_install_page(PAL_USER | PAL_ZERO, 
                                next_upage, 
                                thread_current()->pagedir, 
                                true,
                                false,
                                NULL,
                                -1);

           if (!k_new_page)
           {
              printf("Cound not allocate new page for stack\n");
              goto failure;
           }


          /* Making the SPT entry for this page */
          struct spt_entry * spe = malloc(sizeof(struct spt_entry));
          spe -> upage = next_upage;
          spe -> location = STACK;
          spe -> writable = true;
          insert_spe(&thread_current()->sp_table, spe);
          return;
        }
      }

    }
 
 failure:
   /* Handle page_faults gracefully for user invalid access. */
   if (t->in_sys_call) 
   {
      f->eip = (void *) f->eax;
      f->eax = 0xffffffff;
      return;
   }

   printf ("Page fault at %p: %s error %s page in %s context.\n",
         fault_addr,
         not_present ? "not present" : "rights violation",
         write ? "writing" : "reading",
         user ? "user" : "kernel"); 
   
   kill (f);
}

/* function called when page faults for FILE_SYS or ALL_ZERO pages */
static bool 
actual_load_page(struct spt_entry *spe)
{  
   // hygeine check
   ASSERT (spe->location == FILE_SYS ||  spe->location == ALL_ZERO);

   struct thread *t = thread_current ();
   uint8_t *kpage;
   enum palloc_flags flags = PAL_USER;
   if (spe->location == ALL_ZERO)
   {
      flags |= PAL_ZERO;
   }

   kpage = get_and_install_page(flags, 
                           spe->upage, 
                           t->pagedir, 
                           spe->writable,
                           spe->location == FILE_SYS,
                           t->file_name,
                           (spe->absolute_off - (spe->absolute_off % PGSIZE)) / PGSIZE);
    
   /* case when the get and install fails */
   if (kpage == NULL)
   { 
      return false;
   } 

   if (spe->location == ALL_ZERO)
   {
      return true;
   }

   /* Load data into the page. */
   struct file *fp = t->exec_file;
   lock_acquire(&file_lock);
   // printf("file_length is %u and abs-off is %u and read bytes %u\n", file_length(fp), spe->absolute_off, spe->page_read_bytes);
   file_seek(fp, spe->absolute_off);
   off_t s;
   if ((s = file_read (fp, kpage, spe->page_read_bytes)) 
         != (int) spe->page_read_bytes)
   {  
      printf("read: %u should have read:%u \n", s, spe->page_read_bytes);
      lock_release(&file_lock);
      return false;
   }
   lock_release(&file_lock);
   memset (kpage + spe->page_read_bytes, 0, PGSIZE - spe->page_read_bytes);
   return true;
}

/* function called when page faults for FILE_SYS or ALL_ZERO pages */
static bool 
actual_load_mmap_page(struct page_mmap_entry *pentry)
{  
   struct thread *t = thread_current ();
   uint8_t *kpage = get_and_install_page(PAL_USER, 
                           pentry->uaddr, 
                           t->pagedir, 
                           true,
                           true,
                           NULL,
                           -1);
   /* case when the get and install fails */
   if (kpage == NULL)
   { 
      return false;
   }

   /* Load data into the page. */
   struct file *fp = pentry->fentry->file_pt;
   lock_acquire(&file_lock);
   file_seek(fp, pentry->offset);
   off_t page_read_bytes = (file_length(fp) - pentry->offset) >= PGSIZE ? PGSIZE : file_length(fp) % PGSIZE;
   file_read (pentry->fentry->file_pt, kpage, page_read_bytes);
   lock_release(&file_lock);
   memset (kpage + page_read_bytes, 0, PGSIZE - page_read_bytes);
   return true;
}

/* pallocs and intsalls upage in the current thread's directory
if not already instlaled (sharing); returns null when fails */
static uint8_t *
get_and_install_page(enum palloc_flags flags, 
                     void *upage, 
                     uint32_t *pagedir, 
                     bool writable,
                     bool is_filesys,
                     char *name,
                     unsigned int page_num)
{
   // intr_disable();
   uint8_t *kpage = pagedir_get_page (pagedir, upage);

   if (kpage == NULL)
   {
    struct owner *frame_owner = malloc(sizeof(struct owner));
    frame_owner->t = thread_current();
    frame_owner->upage = upage;

    if (is_filesys && !writable)
    {
      lock_acquire(&frame_lock);
      lock_acquire(&share_lock);
      void *kpage = find_sharing_entry(&share_table, name, page_num);
      if (kpage)
      {
        /* Add the page to the process's address space. */
        if (!install_page (upage, kpage, writable)) 
        {
          free(frame_owner);
          return NULL; 
        }
        struct frame_entry *kframe_entry = find_frame_entry(&frame_table, kpage);
        list_push_back(&kframe_entry->owners, &frame_owner->elem);
        kframe_entry->owners_list_size++;
        lock_release(&share_lock);
        lock_release(&frame_lock);
        return kpage;
      }      
      lock_release(&share_lock);
      lock_release(&frame_lock);
    }
     /* Get a new page of memory. */
     kpage = palloc_get_page (flags);
     if (kpage == NULL)
     {   
      free(frame_owner);
      return NULL;
     }
      
     struct frame_entry *kframe_entry;

   lock_acquire(&frame_lock);
   kframe_entry = find_frame_entry(&frame_table, kpage);
   list_push_back(&kframe_entry->owners, &frame_owner->elem);
   kframe_entry->owners_list_size++;
   lock_release(&frame_lock);

     /* Add the page to the process's address space. */
     if (!install_page (upage, kpage, writable)) 
     {
      palloc_free_page (kpage);
      return NULL; 
     }

    if (is_filesys && !writable) {
      lock_acquire(&frame_lock);
      lock_acquire(&share_lock);
      ASSERT(kframe_entry);
      kframe_entry->inner_entry 
         = insert_sharing_entry(&share_table, name, page_num, kpage);
      lock_release(&share_lock);
      lock_release(&frame_lock);
    }
   } 
   else 
   {  
     /* Check if writable flag for the page should be updated */
     if(writable && !pagedir_is_writable(pagedir, upage))
      {
       pagedir_set_writable(pagedir, upage, writable); 
      }
   }
   return kpage;
}