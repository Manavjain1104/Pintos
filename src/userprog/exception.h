#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

#include "threads/palloc.h"
#include <inttypes.h>
#include "lib/stdbool.h"


/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /* 0: read, 1: write. */
#define PF_U 0x4    /* 0: kernel, 1: user process. */

void exception_init (void);
void exception_print_stats (void);
uint8_t *
get_and_install_page(enum palloc_flags flags, 
                     void *upage, 
                     uint32_t *pagedir, 
                     bool writable,
                     bool is_filesys,
                     char *name,
                     unsigned int page_num);

#endif /* userprog/exception.h */
