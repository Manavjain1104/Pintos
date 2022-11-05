#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define NUM_SYS_CALLS 20

void syscall_init (void);

/* defining types for common system calls */

int open(const char *file);

#endif /* userprog/syscall.h */
