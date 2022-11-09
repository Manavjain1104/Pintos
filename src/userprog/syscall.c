#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include "lib/kernel/hash.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static int get_word (const uint8_t *uaddr);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);

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
  printf ("system call!\n");

  /* verifying and reading value at esp */
  int sys_call_num = get_word(f->esp);

  printf("sys_call_num: %d\n", sys_call_num);
  handlers[sys_call_num] (f);
  printf("ended system call %d\n\n", sys_call_num);
  // ASSERT(false);
  // thread_exit ();
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
    if (!is_user_vaddr(uaddr + i))
    {
      return -1;
    }  

    byte = get_user(uaddr + i);
    printf("byte: %x\n", byte);

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

/* system call functions */
void 
halt_handler(struct intr_frame *f UNUSED) {
  printf("HALTING! \n");
  shutdown_power_off();
}


void 
exec_handler(struct intr_frame *f) {
  int word = get_word(f->esp + sizeof(void *));
  if (word < 0)
  {
    f->eax = 0xffffffff;
    return;
  } 
  f->eax = process_execute((char *) word);
}

void
exit_handler(struct intr_frame *f) {
  intr_disable();
  printf("exiting %s\n", thread_current()->name);
  struct baby_sitter *bs = thread_current()->nanny;
  if (bs != NULL)
  {
    // this means that parent is alive and might need visibility of exit_status
    bs->exit_status = get_word(f->esp + sizeof(void *));
  }
  printf("Set exit status as: %d\n", bs->exit_status);
  thread_exit();
}

void
wait_handler(struct intr_frame *f) {
  enum intr_level old_level;
  old_level = intr_disable();  // TODO: ask Mark???
  int child_pid = get_word(f->esp + sizeof(void *));
  printf("Thread %s waiting on pid %d\n", thread_current()->name, child_pid);
  if (child_pid == -1)
  {
    f->eax = 0xffffffff;
    return;
  }
  f->eax = process_wait(child_pid);
  intr_set_level(old_level);
}

void
create_handler(struct intr_frame *f UNUSED) {}

void
remove_handler(struct intr_frame *f UNUSED) {}

void
open_handler(struct intr_frame *f UNUSED) {}
void
filesize_handler(struct intr_frame *f UNUSED) {}
void
read_handler(struct intr_frame *f UNUSED) {}
void
write_handler(struct intr_frame *f UNUSED) {
  printf("write called!\n");
}
void
seek_handler(struct intr_frame *f UNUSED) {}
void
tell_handler(struct intr_frame *f UNUSED) {}
void
close_handler(struct intr_frame *f UNUSED) {}


