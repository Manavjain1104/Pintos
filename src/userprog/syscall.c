#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <stdio.h>
#include "lib/kernel/console.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static int get_word (const uint8_t *uaddr);
static int get_user (const uint8_t *uaddr);
static int get_byte (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static void put_byte (uint8_t *udst, uint8_t byte);
static int allocate_fd (void);
static void delete_thread (void);
static struct fd_st *get_fd (int fd);

/* struct to store child-parent relationship */

/* type of functions for sys call handlers */
typedef intr_handler_func syscall_handler_func;

/* handler function definitions */
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

/* array of syscall structs respective system calls */
static syscall_handler_func *handlers[NUM_SYS_CALLS];

void
syscall_init (void) 
{
  intr_register_int (SYSCALL_INTR_NUM, 3, INTR_ON, syscall_handler, "syscall");
  
  /* intialising the handlers array with sys call structs */
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
}

static void
syscall_handler (struct intr_frame *f) 
{
  f->esp_dummy = 1;
  /* verifying and reading value at esp */
  int sys_call_num = get_word(f->esp);

  // printf("sys_call_num: %d\n", sys_call_num);
  handlers[sys_call_num] (f);
  // printf("ended system call %d\n", sys_call_num);

  f->esp_dummy = 0;
  // TODO: ask mark file deny write to executable
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
  for (int i = (WORD_LENGTH - 1); i >= 0; i--) {
    byte = get_byte(uaddr + i);
    // printf("byte: %x\n", ibyte);
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
  if (is_user_vaddr(uaddr)) {
    int byte = get_user(uaddr);
    if (byte != -1) {
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
static void
put_byte (uint8_t *udst, uint8_t byte)
{
  
  if (is_user_vaddr(udst))
  {
    if (put_user(udst, byte))
    {
      return;
    }
  }
  delete_thread();
}

/* system call functions */
void 
halt_handler(struct intr_frame *f UNUSED) 
{
  printf("HALTING! \n");
  shutdown_power_off();
}


void 
exec_handler(struct intr_frame *f) 
{
  int word = get_word(f->esp + sizeof(void *));
  // if (word < 0)
  // {
  //   f->eax = 0xffffffff;
  //   return;
  // } 
  f->eax = process_execute((char *) word);
}

void
exit_handler(struct intr_frame *f) 
{
  intr_disable();
  // printf("exiting %s\n", thread_current()->name);
  struct baby_sitter *bs = thread_current()->nanny;
  thread_current()->exit_status = get_word(f->esp + sizeof(void *));
  if (bs != NULL)
  {
    // this means that parent is alive and might need visibility of exit_status
    bs->exit_status = thread_current()->exit_status;
  }
  thread_exit();
}

void
wait_handler(struct intr_frame *f) 
{
  enum intr_level old_level;
  old_level = intr_disable();  // TODO: ask Mark???
  int child_pid = get_word(f->esp + sizeof(void *));
  // printf("Thread %s waiting on pid %d\n", thread_current()->name, child_pid);
  // if (child_pid == -1)
  // {
  //   f->eax = 0xffffffff;
  //   return;
  // }
  f->eax = process_wait(child_pid);
  intr_set_level(old_level);
}

void
open_handler(struct intr_frame *f) 
{
  int word = get_word(f->esp + sizeof(void *));
  // if (word == -1) 
  // {
  //   f->eax = 0xffffffff;
  //   return;
  // }
  // TODO SYNCHRONISATION
  struct fd_st *fd_obj = malloc(sizeof(struct fd_st));
  fd_obj->fd = allocate_fd();
  fd_obj->file_pt = filesys_open((const char *)word);
  list_push_back(&thread_current()->fds, &fd_obj->elem);
  f->eax = fd_obj->fd;
}

void
filesize_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj;
  // if (fd == -1 || (fd_obj = get_fd(fd)) == NULL)
  if ((fd_obj = get_fd(fd)) == NULL)
  {
    f->eax = 0xffffffff;
    return;
  }

  f->eax = file_length(fd_obj->file_pt);
}

void
read_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  int buffer = get_word(f->esp + sizeof(void *) * 2);
  int size = get_word(f->esp + sizeof(void *) * 3);
  if (//fd == -1 
      //|| size == -1 
      //|| buffer == -1
      fd == STDOUT_FILENO)
  {
    f->eax = 0xffffffff;
    return;
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

  /* create a fd object only when necessary */
  struct fd_st *fd_obj = get_fd(fd);
  if (fd_obj == NULL)
  {
    f->eax = 0xffffffff;
    return;
  }

  /* create a temporary buffer for reading using file struct */
  uint8_t *temp_buf = malloc(size * sizeof(uint8_t));
  int actual_read = file_read(fd_obj->file_pt, temp_buf, size);

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
  if (//fd == -1 
      //|| size == -1 
      // || buffer == -1
      fd == STDIN_FILENO)
  {
    f->eax = 0;
    return; 
  }
  
  /* writing from user mem buffer to temp buffer */
  uint8_t *temp_buffer = malloc(size * sizeof(uint8_t));
  for (int i = 0; i < size; i++)
  { 
    temp_buffer[i] = (uint8_t) get_byte((const uint8_t *)buffer + i);
    // printf("temp_buffer[%d]: %u\n", i, temp_buffer[i]);
  }
  
  if (fd == STDOUT_FILENO) 
  {
    /* writing out to putbuf in multiples of STDOUT_MAX_BUFFER_SIZE */
    for (int i = 0; i < size; i += STDOUT_MAX_BUFFER_SIZE)
    { 
      size_t actual_size = (size - i * STDOUT_MAX_BUFFER_SIZE) < STDOUT_MAX_BUFFER_SIZE 
      ? (size - i * STDOUT_MAX_BUFFER_SIZE)
      : STDOUT_MAX_BUFFER_SIZE; 
      // printf("actual size: %d\n", actual_size);
      putbuf ((const char *) temp_buffer + i, actual_size);
      // printf("put bye num: %d\n", i);
    }
    f->eax = size;
    return;
  }

  /* create a fd object only when necessary */
  struct fd_st *fd_obj = get_fd(fd);
  if (fd_obj == NULL)
  {
    f->eax = 0;  // TODO: ask MARK is 0 or -1
    return;
  }

  /* write out to file */
  f->eax = file_write(fd_obj->file_pt, temp_buffer, size);
  free(temp_buffer);
}

void
create_handler(struct intr_frame *f) 
{
  int file_name = get_word(f->esp + sizeof(void *));
  int initial_size = get_word(f->esp + sizeof(void *) * 2);
  // if (file_name == -1 || initial_size == -1) 
  // {
  //   f->eax = false;
  //   return;
  // }

  f->eax = filesys_create ((const char *) file_name, initial_size);
}

void
remove_handler(struct intr_frame *f) 
{
  int file_name = get_word(f->esp + sizeof(void *));
  // if (file_name == -1) 
  // {
  //   f->eax = false;
  //   return;
  // }
  f->eax = filesys_remove ((const char *) file_name);
}

void
seek_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  int new_pos = get_word(f->esp + sizeof(void *) * 2);
  struct fd_st *fd_obj;
  if (// fd == -1 || new_pos == -1 ||
  (fd_obj = get_fd(fd)) == NULL)
  {
    // TODO: ask Mark what to do here???
    return;
  }
  file_seek(fd_obj->file_pt, (unsigned) new_pos);
}

void
tell_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj;
  if (// fd == -1 ||
   (fd_obj = get_fd(fd)) == NULL)
  {
    return;
  }
  f->eax = file_tell(fd_obj->file_pt);
}

void
close_handler(struct intr_frame *f) 
{
  int fd = get_word(f->esp + sizeof(void *));
  struct fd_st *fd_obj;
  if (// fd == -1 ||
  (fd_obj = get_fd(fd)) == NULL)
  {
    return;
  }
  file_close(fd_obj->file_pt);
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

/* Returns 'struct fd' if fd is valid for current thread 
   else returns null */
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

static
void delete_thread (void) {
  thread_current()->exit_status = -1;
  if (thread_current()->nanny != NULL)
  {
   thread_current()->nanny->exit_status = -1;
  }
  thread_exit();
}