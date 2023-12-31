#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/spt.h"
#include "vm/mmap.h"

static thread_func start_process NO_RETURN;
static bool load (char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy, *name;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
  {
    return TID_ERROR;
  }
  strlcpy (fn_copy, file_name, PGSIZE);
  
  /* extract out command name for the thread name */
  name = malloc (sizeof(char) * MAX_FILE_NAME_SIZE + 1);
  if (name == NULL)
  {
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }

  strlcpy (name, file_name, sizeof(char) * MAX_FILE_NAME_SIZE + 2);
  char *fakeptr;

  strtok_r(name, " ", &fakeptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (name, PRI_DEFAULT, start_process, fn_copy);

  free(name);

  if (tid == TID_ERROR) 
  {
    palloc_free_page (fn_copy);
    return TID_ERROR;
  }
    

  struct list_elem *e;
  for (e = list_begin(&thread_current()->baby_sitters); 
       e != list_end(&thread_current()->baby_sitters);
       e = list_next(e))
  { 
    struct baby_sitter *bs = list_entry(e, struct baby_sitter, elem); 
    if (bs->child_tid == tid)
    {
      sema_down (&bs->start_process_sema);
      if (!bs->start_process_success) 
      {
        return TID_ERROR; 
      }
      break;
    }
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *fn_copy)
{ 
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (fn_copy, &if_.eip, &if_.esp);
  thread_current()->nanny->start_process_success = success;

  /* If load failed, quit. */
  enum intr_level old_level = intr_disable();
  palloc_free_page (fn_copy);
  sema_up (&thread_current()->nanny->start_process_sema);
  if (!success)
  { 
    delete_thread(-1);
  }
  intr_set_level(old_level);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status. 
 * If it was terminated by the kernel (i.e. killed due to an exception), 
 * returns -1.  
 * If TID is invalid or if it was not a child of the calling process, or if 
 * process_wait() has already been successfully called for the given TID, 
 * returns -1 immediately, without waiting.
 */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct list_elem *e;
  struct baby_sitter *bs;
  struct list *bss = &thread_current()->baby_sitters;
  for (e = list_begin(bss);
       e != list_end(bss);
       e = list_next(e))
  { 
    bs = list_entry(e, struct baby_sitter, elem);
    if (bs->child_tid == child_tid)
    {
      /* It must be a valid child, parent should wait for it to exit*/
      sema_down(&bs->sema);

      /* Now child has exited*/ 
      list_remove(&bs->elem);
      int exit_status = bs->exit_status;
      free(bs);                                                 
      return exit_status;
    }
  }
  /* Did not find valid child*/ 
  return -1; 
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();

  uint32_t *pd;
      
  /* destroy supplemental page_table */
  bool prev_frame = re_lock_acquire(&frame_lock);
  bool prev_spt = re_lock_acquire(&cur->spt_lock);
  destroy_spt_table(&cur->sp_table);
  re_lock_release(&cur->spt_lock, prev_spt);

  destroy_mmap_tables();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
  re_lock_release(&frame_lock, prev_frame);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
// #define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char *fn_copy, char *saveptr);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* supplemental page table intialisation */
  lock_init(&t->spt_lock);
  lock_acquire(&frame_lock);
  lock_acquire(&t->spt_lock);
  if (!generate_spt_table(&t->sp_table))
  {
    lock_release(&t->spt_lock);
    lock_release(&frame_lock);
    return false;
  }
  lock_release(&t->spt_lock);

  /* Memory mapped files table initialization */
  if (!generate_mmap_tables(&t->page_mmap_table, &t->file_mmap_table))
  {
    lock_release(&frame_lock);
    return false;
  }

  t->mapid_next = 0;

  /* Parsing the file name from the fn_copy */
  char *saveptr;

  /* Open executable file. */
  lock_acquire(&file_lock);
  file = filesys_open (strtok_r(file_name, " ", &saveptr));
  if (file == NULL) 
    {
      printf("load: %s: open failed\n", thread_current()->name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  t->exec_file = filesys_open(file_name);
  file_deny_write(t->exec_file);
  
  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        {
          goto done;
        }
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        {
          goto done;
        }
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  
  strlcpy(t->file_name, file_name, MAX_FILE_NAME_SIZE);

  /* Set up stack. */
  if (!setup_stack (esp, file_name, saveptr))
  {
    goto done;
  }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  lock_release(&file_lock);
  lock_release(&frame_lock);
  return success;
}

/* load() helpers. */


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  int pg_num = 0;
  size_t last_page_read_bytes = 0;
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
          
      /* LAZY LOADING */
      struct thread *t = thread_current();
      lock_acquire(&t->spt_lock);
      struct spt_entry *spe = malloc(sizeof(struct spt_entry));
      spe->upage = upage;
      spe->writable = writable;
      spe->page_read_bytes = page_read_bytes;
      spe->absolute_off = ofs + last_page_read_bytes;
      spe->location = (page_read_bytes == 0) ? ALL_ZERO : FILE_SYS;

      
      struct hash_elem *he = insert_spe(&t->sp_table, spe);
      if (he)
      {
        // this means an equal element is already in the hash table
        update_spe(hash_entry(he, struct spt_entry, elem), spe);
        free(spe);
      }
      lock_release(&t->spt_lock);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      last_page_read_bytes += page_read_bytes;
      pg_num++;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char *fn_copy, char *saveptr)
{
  struct thread *t = thread_current();
  uint8_t *kpage = get_and_install_page(PAL_USER | PAL_ZERO, 
                       ((uint8_t *) PHYS_BASE) - PGSIZE,
                       t->pagedir,
                       true, false, NULL, -1);
  ASSERT(kpage);
  if (kpage != NULL) 
    { 
      lock_acquire(&t->spt_lock);
      *esp = PHYS_BASE;
      /* Establishing initial stack page for current thread */
      struct spt_entry * spe = malloc(sizeof(struct spt_entry));
      spe -> upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
      spe -> location = STACK;
      spe -> writable = true;
      ASSERT(!insert_spe(&t->sp_table, spe));
      lock_release(&t->spt_lock);

      /* Total bytes required for stack setup */
      unsigned total_bytes = strlen(fn_copy) + 1;

      int argc = 1;
      char *pt;
      bool prevspace = false;
      for (pt = saveptr; *pt != '\0'; pt++) {
        if (*pt == ' ')
        {
          if (!prevspace)
          { 
            argc++;
            total_bytes++;
          }
          prevspace = true;
        } else {
          prevspace = false;
          total_bytes++;
        }
      }
      if (prevspace)
      {
        argc--;
      }
      if (pt > saveptr)
      {
        argc++;
        total_bytes++;
      }

      /* Word_align the top arguments and set argv[argc] to null */
      int len_align = 0;
      if ((total_bytes % WORD_LENGTH) > 0) 
      {
        len_align = WORD_LENGTH - (total_bytes % WORD_LENGTH);
      }

      total_bytes += len_align 
                      + (argc + 1) * sizeof(char *) 
                      + sizeof(char **) 
                      + sizeof(int) 
                      + sizeof(void *);

      /* Pre-checking for overflow in user stack */
      if (PGSIZE < total_bytes) 
      {
        return false;
      }

      /* Start updating user stack */
      char **arg_pt_arr = (char **) malloc(sizeof(char*) * argc);
      char *arg;
      int len = strlen(fn_copy) + 1;
      *esp = *esp - len;
      arg_pt_arr[0] = *esp;
      strlcpy(*esp, fn_copy, len);

      /* set up arguments on the top of stack */
      int i = 1;
      while ((arg = strtok_r(NULL, " ", &saveptr))) 
      {
        if (strlen(arg) == 0)
        {
          continue;
        }
        len = strlen(arg) + 1;
        *esp = *esp - len;
        arg_pt_arr[i] = (char *) *esp;
        i++;
        strlcpy(*esp, arg, len);
      }

      /* Check that strtok_r got the right number of args */

      ASSERT(argc == i); 
      *esp = *esp - len_align - sizeof(char *);


      /* Set up the pointer to arguments */
      for (i = argc - 1; i >= 0; i--) 
      {
        *esp = *esp - sizeof(char *);
        memcpy(*esp, &arg_pt_arr[i], sizeof(char *));
      }

      free(arg_pt_arr);

      /* Set up argv and argc on the stack */
      memcpy((*esp - sizeof(char *)), esp, sizeof(char *));
      *esp = *esp - sizeof(char *) - sizeof(int);
      memcpy(*esp, &argc, sizeof(int));
      /* Set up fake return address */
      *esp = *esp - sizeof(void *);
      return true;
    } 
  return false;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}