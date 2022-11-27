#ifndef MMAP_H
#define MMAP_H

#include "lib/user/syscall.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
#include "userprog/syscall.h"

struct page_mmap_entry {
    void *uaddr;
    bool written;
    struct file_mmap_entry *fentry;
    struct hash_elem helem;
    struct list_elem lelem;
};

struct file_mmap_entry {
    mapid_t mapping;
    int fd; 
    struct list *page_mmap_entries;
    struct hash_elem elem;
};

bool generate_mmap_tables(struct hash *page_mmap_table,\
                          struct hash *file_mmap_table);
/* Returns -1 if not upage not found */
int get_page_fd(struct hash *page_mmap_table,
                struct hash *file_mmap_table, void *upage);
mapid_t insert_mmap(struct hash *page_mmap_table, struct hash *file_mmap_table,
                    void *uaddr, struct fd_st *fd_obj);
void unmap_entry(struct hash *page_mmap_table, struct hash *file_mmap_table,
                 mapid_t mapping);
void destroy_mmap_tables(void);

// TODO: add to page_fault + implement get_fd + thread_exit + write + unmap write back to file
// TODO concurrency: lock on allocate_mapid + concurrency overlap + insert
#endif /* vm/mmap.h */