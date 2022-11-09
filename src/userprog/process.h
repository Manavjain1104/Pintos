#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* struct to maintain a child-parent relationship for exit statuses */
struct baby_sitter {
    struct thread *child;
    int exit_status;
    int child_tid;

    /* to allow parent to block itself */
    struct semaphore *sema;

    /* to allow this struct to be part of a children list in parent */
    struct list_elem elem;   
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void free_baby_sitter(struct baby_sitter *bs);

#endif /* userprog/process.h */
