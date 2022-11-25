#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

bool install_page (void *upage, void *kpage, bool writable);

/* struct to maintain a child-parent relationship for exit statuses */
struct baby_sitter {
    struct thread *child;
    int exit_status;
    int child_tid;

    /* to allow parent to block itself */
    struct semaphore sema;

    /* semaphore to handle parent wait until start of child process */
    struct semaphore start_process_sema;

    /* flag set to true if and only if start_process of child sucessful */
    bool start_process_success;

    /* to allow this struct to be part of a children list in parent */
    struct list_elem elem;   
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
