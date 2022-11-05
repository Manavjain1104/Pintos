#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static int get_word (const uint8_t *uaddr);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);

/* type of functions for sys call handlers */
typedef void syscall_handler_func (struct intr_frame *, int num_args);

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

/* struct to store function pointer for system call
  along with the number of arguments */
struct syscall {
  syscall_handler_func *func;
  int num_args;
};

/* array of syscall structs respective system calls */
static struct syscall handlers[NUM_SYS_CALLS];

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  /* intialising the handlers array with sys call structs */
  handlers[SYS_HALT] = (struct syscall) {halt_handler, 0};            
  handlers[SYS_EXIT] = (struct syscall) {exit_handler, 1};                  
  handlers[SYS_EXEC] = (struct syscall) {exec_handler, 1};
  handlers[SYS_WAIT] = (struct syscall) {wait_handler, 1};                 
  handlers[SYS_CREATE] = (struct syscall) {create_handler, 2};                
  handlers[SYS_REMOVE] = (struct syscall) {remove_handler, 1};               
  handlers[SYS_OPEN] = (struct syscall) {open_handler, 1};                 
  handlers[SYS_FILESIZE]  = (struct syscall) {filesize_handler, 1};             
  handlers[SYS_READ] = (struct syscall) {read_handler, 3};                 
  handlers[SYS_WRITE] = (struct syscall) {write_handler, 3};               
  handlers[SYS_SEEK] = (struct syscall) {seek_handler, 2};                   
  handlers[SYS_TELL] = (struct syscall) {tell_handler, 1};                    
  handlers[SYS_CLOSE] = (struct syscall) {close_handler, 1};  
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");

  /* verifying and reading value at esp */
  int sys_call_num = get_word(f->esp);

  printf("sys_call_num: %d\n", sys_call_num);
  handlers[sys_call_num].func (f, handlers[sys_call_num].num_args);
  
  ASSERT(false);
  thread_exit ();
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
  for (int i = 3; i >= 0; i--) {
    if (!is_user_vaddr(uaddr + i))
      thread_exit();

    byte = get_user(uaddr + i);
    printf("byte: %x\n", byte);

    if (byte < 0) 
     thread_exit();

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

/* helper function that fetches 

/* system call functions */

void 
halt_handler(struct intr_frame *f, int num_args) {
  shutdown_power_off()
}

void 
exec_handler(struct intr_frame *f, int num_args) {
  f->eax = process_execute((char *) get_word(f->esp + sizeof(void *));
}

/* NOTE: CHICK-CHICK
  1) TID == PID ?
  2) EAX OR DEFERENCED EAX in page fault as well as SYS CALLS
  3) we have fixed page_faults but if its called how is it called 
*/

