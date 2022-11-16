#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/kernel/list.h"

#define NUM_SYS_CALLS 20
#define SYSCALL_INTR_NUM 0x30
#define STDOUT_MAX_BUFFER_SIZE 500
#define MAX_FILE_NAME_SIZE 14
#define USER_STACK_LOWER_BOUND 0xbffff000

extern struct lock file_lock;

/* struct for the file descriptor objects owned by threads */
struct fd_st {
    int fd;
    struct file *file_pt;
    struct list_elem elem;
};

void syscall_init (void);
void delete_thread (int exit_stat);

#endif /* userprog/syscall.h */
