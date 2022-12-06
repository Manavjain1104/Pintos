#ifndef MMAP_H
#define MMAP_H

#include "lib/user/syscall.h"
#include "lib/kernel/list.h"
#include "lib/kernel/hash.h"
#include "userprog/syscall.h"

struct page_mmap_entry {
    void *uaddr;
    struct file_mmap_entry *fentry;
    unsigned offset;
    struct hash_elem helem;
    struct list_elem lelem;
};

struct file_mmap_entry {
    mapid_t mapping;
    struct file *file_pt; 
    char file_name[MAX_FILE_NAME_SIZE];
    struct list *page_mmap_entries;
    struct hash_elem elem;
};

bool generate_mmap_tables(struct hash *page_mmap_table,\
                          struct hash *file_mmap_table);
/* Returns NULL if not upage not found */
struct page_mmap_entry *get_mmap_page(struct hash *page_mmap_table, void *upage);
mapid_t insert_mmap(struct hash *page_mmap_table, struct hash *file_mmap_table,
                    void *uaddr, struct fd_st *fd_obj);
void unmap_entry(struct hash *page_mmap_table, struct hash *file_mmap_table,
                 struct file_mmap_entry *fentry, bool delete_from_table);
void destroy_mmap_tables(void);

#endif /* vm/mmap.h */