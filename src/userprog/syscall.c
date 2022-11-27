#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <stdio.h>
#include "lib/kernel/console.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "lib/stdio.h"
#include "userprog/pagedir.h"
#include "vm/spt.h"
#include "vm/mmap.h"

static void syscall_handler (struct intr_frame *);
static int get_word (const uint8_t *uaddr);
static int get_user (const uint8_t *uaddr);
static int get_byte (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static bool put_byte (uint8_t *udst, uint8_t byte);
static int allocate_fd (void);
static struct fd_st *get_fd (int fd);
static bool validate_filename(const uint8_t * word);

/* Struct to store child-parent relationship */
struct lock file_lock; 

/* Type of functions for sys call handlers */
typedef intr_handler_func syscall_handler_func;

/* Handler function definitions */
syscall_handler_func halt_handler;
syscall_handler_func exit_handler;
syscall_handler_func exec_handler;
syscall_handler_func wait_handler;
syscall_handler_func create_handler;
syscall_handler_func remove_handler;
syscall_handler_func open_handler;
syscall_handler_func filesize_handler;
syscall_handler_func read_handler;
syscall_handler_func write_handler;
syscall_handler_func seek_handler;
syscall_handler_func tell_handler;
syscall_handler_func close_handler;
syscall_handler_func mmap_handler;
syscall_handler_func munmap_handler;

/* Array of syscall structs respective system calls */
static syscall_handler_func *handlers[NUM_SYS_CALLS];

void
syscall_init (void) 
{
  intr_register_int (SYSCALL_INTR_NUM, 3, INTR_ON, syscall_handler, "syscall");

  lock_init(&file_lock);
  
  /* Intialising the handlers array with sys call structs */
  handlers[SYS_HALT] = &halt_handler;            
  handlers[SYS_EXIT] = &exit_handler;                  
  handlers[SYS_EXEC] = &exec_handler;
  handlers[SYS_WAIT] = &wait_handler;                 
  handlers[SYS_CREATE] = &create_handler;                
  handlers[SYS_REMOVE] = &remove_handler;               
  handlers[SYS_OPEN] = &open_handler;                 
  handlers[SYS_FILESIZE] = &filesize_handler;             
  handlers[SYS_READ] = &read_handler;                 
  handlers[SYS_WRITE] = &write_handler;               
  handlers[SYS_SEEK] = &seek_handler;                   
  handlers[SYS_TELL] = &tell_handler;                    
  handlers[SYS_CLOSE] = &close_handler;
  handlers[SYS_MMAP] = &mmap_handler;
  handlers[SYS_MUNMAP] = &munmap_handler;
}

static void
syscall_handler (struct intr_frame *f) 
{  
  /* Setting sys_cal flag */
  thread_current()->in_sys_call = true;

  /* Verifying and reading value at esp */
  int sys_call_num = get_word(f->esp);

  if (sys_call_num < 0 || sys_call_num >= NUM_SYS_CALLS)
  {
    delete_thread(-1);
  }

  handlers[sys_call_num] (f);

  thread_current()->in_sys_call = false;
}

/* Reads a word at user virtual address UADDR.
   UADDR must be below PHYS_BASE. 
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int 
get_word (const uint8_t *uaddr) 
{ 
  int word = 0;
  int byte;
  for (int i = (WORD_LENGTH - 1); i >= 0; i--) 
  {
    byte = get_byte(uaddr + i);
    if (byte == -1) 
    {
      return -1;
    }
    word += byte << (i * 8);
  }
  return word;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}


/* Reads a byte at user virtual address UADDR.
   Returns the byte value if successful, -1 if a segfault
   occurred. (wrapper for get_user function)
   NOTE:- Trigger a page_fault.*/
static int
get_byte (const uint8_t *uaddr)
{
  if (is_user_vaddr(uaddr)) 
  {
    int byte = get_user(uaddr);
    if (byte != -1) 
    {
      return byte;
    }
  }
  return -1;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* Writes BYTE to user address UDST.
   If fails to write at user address, sets exit status to -1 and thread exits.
   NOTE:- Does not trigger page_fault */
static bool
put_byte (uint8_t *udst, uint8_t byte)
{
  return is_user_vaddr(udst) && put_user(udst, byte);
}

/* System call functions */
void 
halt_handler(struct intr_frame *f UNUSED) 
{
  printf("HALTING!\n");
  shutdown_power_off();
}


void 
exec_handler(struct intr_frame *f) 
{    
  int word = get_word(f->esp + sizeof(void *));

  if (word == -1 || !validate_filename((const uint8_t *) word))
  {
    // Problem with the data provided
    f->eax = -1;
    return;
  }
  int tid = process_execute((const char *) word);
  f->eax = tid;
}

void
exit_handler(struct intr_frame *f) 
{
  intr_disable();
  struct baby_sitter *bs = thread_current()->nanny;
  thread_current()->exit_status = get_word(f->esp + sizeof(void *));
  if (bs != NULL)
  {
    //This means that parent is alive and might need visibility of exit_status
    bs->exit_status = thread_current()->exit_status;
  }
  thread_exit();
}

void
wait_handler(struct intr_frame *f) 
{
  enum intr_level old_level;
  old_level = intr_disable();
  int child_pid = get_word(f->esp + sizeof(void *));
  f->eax = process_wait(child_pid);
  intr_set_level(old_level);
}

void
open_handler(struct intr_frame *f) 
{ 
  
  int word = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj = malloc(sizeof(struct fd_st));
  
  lock_acquire(&file_lock);
  fd_obj->fd = allocate_fd();
  if (word == -1 || !validate_filename((const uint8_t *) word))
  { 
    lock_release(&file_lock);
    free(fd_obj);
    delete_thread(-1);
  }

  fd_obj->file_pt = filesys_open((const char *)word);
  
  if (!fd_obj->file_pt)
  {
    lock_release(&file_lock);
    free(fd_obj);
    thread_current()->exit_status = 0;

    enum intr_level old_level = intr_disable();
    if (thread_current()->nanny != NULL)
    {
      thread_current()->nanny->exit_status = 0;
    }
    intr_set_level(old_level);

    f->eax = -1;
    return;
  }
  
  list_push_back(&thread_current()->fds, &fd_obj->elem);
  lock_release(&file_lock);
  
  f->eax = fd_obj->fd;
}

void
filesize_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj;

  lock_acquire(&file_lock);
  
  if ((fd_obj = get_fd(fd)) == NULL)
  {
    lock_release(&file_lock);
    f->eax = 0xffffffff;
    return;
  }
  
  f->eax = file_length(fd_obj->file_pt);
  lock_release(&file_lock);
}

void
read_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  int buffer = get_word(f->esp + sizeof(void *) * 2);
  int size = get_word(f->esp + sizeof(void *) * 3);
  if (fd == -1 
      || size < 0  
      || buffer == -1
      || fd == STDOUT_FILENO
      || !is_user_vaddr((void *) buffer))
  {
    delete_thread(-1);
  }
  
  if (fd == STDIN_FILENO) 
  { 
    for (int i = 0; i < size; i++)
    {
      put_byte((uint8_t *)buffer + i, input_getc());
    }
    f->eax = size;
    return;
  }

  /* Create a fd object only when necessary */
  lock_acquire(&file_lock);
  struct fd_st *fd_obj = get_fd(fd);
  if (fd_obj == NULL)
  {
    f->eax = 0xffffffff;
    lock_release(&file_lock);
    return;
  }

  /* Create a temporary buffer for reading using file struct */
  uint8_t *temp_buf = malloc(size * sizeof(uint8_t));
  int actual_read = file_read(fd_obj->file_pt, temp_buf, size);
  lock_release(&file_lock);

  for (int i = 0; i < actual_read; i++)
  {
    put_byte((uint8_t *)buffer + i, temp_buf[i]);
  }

  free(temp_buf);
  f->eax = actual_read;
}

void
write_handler(struct intr_frame *f UNUSED) 
{ 
  int fd = get_word(f->esp + sizeof(void *));
  int buffer = get_word(f->esp + sizeof(void *) * 2);
  int size = get_word(f->esp + sizeof(void *) * 3);

  if (fd <= STDIN_FILENO 
      || size < 0 
      || buffer == -1)
  { 
    delete_thread(-1);
  }
  
  /* Writing from user mem buffer to temp buffer */
  uint8_t *temp_buffer = malloc(size * sizeof(uint8_t));
  for (int i = 0; i < size; i++)
  { 
    int byte = get_byte((const uint8_t *) buffer + i);
    if (byte == -1)
    {
      delete_thread(-1);
    }
    temp_buffer[i] = (uint8_t) byte;
  }
  
  if (fd == STDOUT_FILENO) 
  {
    /* Writing out to putbuf in multiples of STDOUT_MAX_BUFFER_SIZE */
    for (int i = 0; i < size; i += STDOUT_MAX_BUFFER_SIZE)
    { 
      size_t actual_size = (size - i * STDOUT_MAX_BUFFER_SIZE) < STDOUT_MAX_BUFFER_SIZE 
      ? (size - i * STDOUT_MAX_BUFFER_SIZE)
      : STDOUT_MAX_BUFFER_SIZE; 
      putbuf ((const char *) temp_buffer + i, actual_size);
    }

    f->eax = size;
    free(temp_buffer);
    return;
  }

  /* Create a fd object only when necessary */
  lock_acquire(&file_lock);
  struct fd_st *fd_obj = get_fd(fd);
  if (fd_obj == NULL)
  { 
    lock_release(&file_lock);
    f->eax = 0;
    free(temp_buffer);
    return;
  }

  /* Write out to file */
  f->eax = file_write(fd_obj->file_pt, temp_buffer, size);
  lock_release(&file_lock);

  if (size == 0)
  {
    f->eax = -1;
    return;
  }

  /* Set written flag of relevant pages to true */
  for (unsigned i = (unsigned) pg_round_down((void *) buffer); i <= pg_round_down((void *) (buffer + size)); i += PGSIZE)
  {
    struct page_mmap_entry fake_mmap_page_entry;
    fake_mmap_page_entry.uaddr = (void *) i;
    struct hash_elem *mmap_pentry = hash_find(&thread_current()->page_mmap_table, &fake_mmap_page_entry.helem);
    if (mmap_pentry) {
      hash_entry(mmap_pentry, struct page_mmap_entry, helem)->written = true;
    }
  }

  free(temp_buffer);
}

void
create_handler(struct intr_frame *f) 
{
  int file_name = get_word(f->esp + sizeof(void *));
  int initial_size = get_word(f->esp + sizeof(void *) * 2);
  
  if (file_name == -1 
      || initial_size == -1 
      || !validate_filename((const uint8_t *) file_name))
  {
    delete_thread(-1);
  } 

  lock_acquire(&file_lock);
  f->eax = filesys_create ((const char *) file_name, initial_size);
  lock_release(&file_lock);
}

void
remove_handler(struct intr_frame *f) 
{
  int file_name = get_word(f->esp + sizeof(void *));

  lock_acquire(&file_lock);
  f->eax = filesys_remove ((const char *) file_name);
  lock_release(&file_lock);
}

void
seek_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  int new_pos = get_word(f->esp + sizeof(void *) * 2);
  struct fd_st *fd_obj;
  
  lock_acquire(&file_lock);
  if (fd == -1 
      || new_pos == -1 
      || (fd_obj = get_fd(fd)) == NULL)
  {
    lock_release(&file_lock);
    return;
  }
  file_seek(fd_obj->file_pt, (unsigned) new_pos);
  lock_release(&file_lock);
}

void
tell_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj;

  lock_acquire(&file_lock);
  if (fd == -1 
      || (fd_obj = get_fd(fd)) == NULL)
  {
    lock_release(&file_lock);
    return;
  }
  f->eax = file_tell(fd_obj->file_pt);
  lock_release(&file_lock);
}

void
close_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj;

  lock_acquire(&file_lock);
  if (fd == -1 
      || (fd_obj = get_fd(fd)) == NULL)
  {
    lock_release(&file_lock);
    return;
  }

  file_close(fd_obj->file_pt);
  lock_release(&file_lock);

  list_remove(&fd_obj->elem);
  free(fd_obj);
}


/* Returns a fd number to use for a open command. */
static int
allocate_fd (void) 
{
  static int next_fd = 2;
  return next_fd++;
}

/* Returns 'struct fd' if fd is valid for current thread else returns null */
static struct fd_st *
get_fd (int fd)
{
  struct list_elem *e;
  struct fd_st * fd_obj;
  struct list *fds = &thread_current()->fds;
  for (e = list_begin(fds);
       e != list_end(fds);
       e = list_next(e))
  { 
    fd_obj = list_entry(e, struct fd_st, elem);
    if (fd_obj->fd == fd)
    {
      return fd_obj;
    }
  }
  return NULL;
}

/* Sets exit status to 'exit_stat' for thread and then performs thread exit */
void delete_thread (int exit_stat) {
  thread_current()->exit_status = exit_stat;

  intr_disable();
  
  if (thread_current()->nanny != NULL)
  {
   thread_current()->nanny->exit_status = exit_stat;
  }
  thread_exit();
}

static bool
validate_filename(const uint8_t * word)
{
  /* Check uptill 14 characters for valid file_name */
  int i = 0;
  int byte = get_byte(word + i);
  while (i < MAX_FILE_NAME_SIZE 
         && byte != -1
         && (char) byte != ' ' 
         && (char) byte != '\0')
  {   
    i++;
    byte = get_byte((const uint8_t *) word + i);
  }

  return byte != -1;
}

void
mmap_handler(struct intr_frame *f)
{
  int fd = get_word(f->esp + sizeof(void *));
  int addr = get_word(f->esp + sizeof(void *) * 2);
  struct fd_st *fd_obj;
  int flength = 0;
  void *last_page = pg_round_down((void *) (addr + flength));

  // TODO: macro for -1
  lock_acquire(&file_lock);
  if (fd == -1
      || addr <= 0
      || ((unsigned) addr) % PGSIZE != 0
      || fd == STDIN_FILENO
      || fd == STDOUT_FILENO
      || ((fd_obj = get_fd(fd)) == NULL)
      || (flength = file_length(fd_obj->file_pt)) == 0
      || !is_user_vaddr((void *) addr)
      || !is_user_vaddr(last_page))
  {
        lock_release(&file_lock);
        f->eax = -1;
        return;
  }
  lock_release(&file_lock);

  struct thread *t = thread_current();

  /* Check for any memory page overlaps */
  for (unsigned i = (unsigned) addr; i <= (unsigned) last_page; i += PGSIZE)
  {
    if (pagedir_get_page(t->pagedir, (void *) i) != NULL
       || contains_upage(&t->sp_table, (void *) i)
       || get_mmap_page(&t->page_mmap_table, (void *) i) != NULL)
    {
      f->eax = -1;
      return;
    }
  }

  f->eax = insert_mmap(&t->page_mmap_table, &t->file_mmap_table, (void *) addr, fd_obj);
}

void
munmap_handler(struct intr_frame *f)
{
  int mapping = get_word(f->esp + sizeof(void *));
  if (mapping == -1)
  {
    f->eax = -1;
    return;
  }

  struct thread *t = thread_current();
  unmap_entry(&t->page_mmap_table, &t->file_mmap_table, mapping);
}