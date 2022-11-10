#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/kernel/list.h"

#define NUM_SYS_CALLS 20
#define SYSCALL_INTR_NUM 0x30
#define STDOUT_MAX_BUFFER_SIZE 500

/* struct for the file descriptor objects owned by threads */
struct fd_st {
    int fd;
    struct file *file_pt;
    struct list_elem elem;
};

void syscall_init (void);

#endif /* userprog/syscall.h */
