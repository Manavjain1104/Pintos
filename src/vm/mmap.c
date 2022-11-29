#include "mmap.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include "threads/malloc.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "lib/kernel/list.h"

static unsigned page_mmap_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool page_mmap_less_func (const struct hash_elem *a, 
                                 const struct hash_elem *b, 
                                 void *aux UNUSED);
static unsigned file_mmap_hash_func(const struct hash_elem *e,
                                    void *aux UNUSED);
static bool file_mmap_less_func (const struct hash_elem *a, 
                                 const struct hash_elem *b, 
                                 void *aux UNUSED);
static int allocate_mapid (struct thread *current);
static void mmap_entry_free_func (struct hash_elem *e, void *aux UNUSED);

bool generate_mmap_tables(struct hash *page_mmap_table,
                          struct hash *file_mmap_table)
{
    return hash_init(page_mmap_table, page_mmap_hash_func,
                     page_mmap_less_func, NULL) &&
           hash_init(file_mmap_table, file_mmap_hash_func,
                     file_mmap_less_func, NULL); 
}

struct page_mmap_entry *get_mmap_page(struct hash *page_mmap_table, void *upage)
{
    struct page_mmap_entry fake_pentry;
    fake_pentry.uaddr = upage;
    struct hash_elem *pentry_he = hash_find(page_mmap_table, &fake_pentry.helem);
    if (!pentry_he)
    {
        return NULL;
    }
    return hash_entry(pentry_he, struct page_mmap_entry, helem);
}

mapid_t insert_mmap(struct hash *page_mmap_table, struct hash *file_mmap_table,
                    void *uaddr, struct fd_st *fd_obj)
{
    struct file_mmap_entry *fentry = malloc(sizeof(struct file_mmap_entry));

    lock_acquire(&file_lock);
    fentry->mapping = allocate_mapid(thread_current());
    fentry->file_pt = file_reopen(fd_obj->file_pt);
    unsigned flength = file_length(fd_obj->file_pt);
    lock_release(&file_lock);

    struct list *map_entries = malloc(sizeof(struct list));
    list_init(map_entries);

    unsigned last_page = (unsigned) pg_round_down(uaddr + flength);
    for (unsigned i = (unsigned) uaddr; i <= last_page; i += PGSIZE) {
        struct page_mmap_entry *pentry = malloc(sizeof(struct page_mmap_entry));
        pentry->uaddr = (void *) i;
        pentry->fentry = fentry;
        pentry->offset = i - (unsigned) uaddr;
        list_push_back(map_entries, &pentry->lelem);
        struct hash_elem *old_he = hash_insert(page_mmap_table, &pentry->helem);
        ASSERT(!old_he);
    }

    fentry->page_mmap_entries = map_entries;
    struct hash_elem *old_fhe = hash_insert(file_mmap_table, &fentry->elem);
    ASSERT(old_fhe == NULL);
    return fentry->mapping;
}

void unmap_entry(struct hash *page_mmap_table, struct hash *file_mmap_table,
                 struct file_mmap_entry *fentry, bool delete_from_table)
{
    struct list_elem *e;
    for (e = list_begin (fentry->page_mmap_entries); e != list_end (fentry->page_mmap_entries);)
    {
        struct page_mmap_entry *pentry = list_entry(e, struct page_mmap_entry, lelem);
        ASSERT(hash_delete(page_mmap_table, &pentry->helem));      
        if (pagedir_is_dirty (thread_current ()->pagedir, pentry->uaddr))
        {
            struct file *fp = pentry->fentry->file_pt;
            lock_acquire(&file_lock);
            file_seek (fp, pentry->offset);
            /* TODO: check if correct to load entire page */
            file_write (fp, pagedir_get_page(thread_current()->pagedir, pentry->uaddr), PGSIZE);
            lock_release(&file_lock);
        }
        e = list_next(e);
        free(pentry);
    }
    if (delete_from_table) {
        hash_delete(file_mmap_table, &fentry->elem);
    }
    lock_acquire(&file_lock);
    file_close(fentry->file_pt);
    lock_release(&file_lock);
    free(fentry->page_mmap_entries);
    free(fentry);
}

/* Destroys all mmap tables for the current thread */
void destroy_mmap_tables(void)
{
    struct thread *t = thread_current();
    hash_clear(&t->file_mmap_table, mmap_entry_free_func);
}

static void mmap_entry_free_func (struct hash_elem *e, void *aux UNUSED)
{
    struct thread *t = thread_current();
    unmap_entry(&t->page_mmap_table,
                &t->file_mmap_table,
                hash_entry(e, struct file_mmap_entry, elem), false);
}

static unsigned page_mmap_hash_func(const struct hash_elem *e,
                                    void *aux UNUSED)
{
    return (unsigned) (hash_entry(e, struct page_mmap_entry, helem) -> uaddr);
}

static bool page_mmap_less_func (const struct hash_elem *a, 
                                 const struct hash_elem *b, 
                                 void *aux UNUSED)
{
    return (hash_entry(a, struct page_mmap_entry, helem) -> uaddr) 
        < (hash_entry(b, struct page_mmap_entry, helem) -> uaddr);
}

static unsigned file_mmap_hash_func(const struct hash_elem *e,
                                    void *aux UNUSED)
{
    return (unsigned) (hash_entry(e, struct file_mmap_entry, elem) -> mapping);
}

static bool file_mmap_less_func (const struct hash_elem *a, 
                                 const struct hash_elem *b, 
                                 void *aux UNUSED)
{
    return (hash_entry(a, struct file_mmap_entry, elem) -> mapping) 
        < (hash_entry(b, struct file_mmap_entry, elem) -> mapping);
}

static int allocate_mapid (struct thread *cur)
{
    cur->mapid_next++;
    return cur->mapid_next;
}