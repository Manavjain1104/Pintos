#include "threads/malloc.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "frame.h"
#include <stdio.h>

static hash_hash_func frame_hash_func;  // hash function for frame table
static hash_less_func frame_less_func;  // hash less function for frame table

static hash_action_func frame_destroy_func;

bool
generate_frame_table(struct hash *frame_table) 
{
    return hash_init(frame_table, frame_hash_func, frame_less_func, NULL);
}

void 
insert_frame(struct hash *frame_table,
             struct list *queue,
             struct frame_entry *frame)
{
    struct hash_elem *he = hash_insert(frame_table, &frame->elem);
    ASSERT(!he);
    list_push_back(queue, &frame->l_elem);
}

struct frame_entry *
evict_frame(struct list *queue,
            struct list_elem **index)
{
    struct frame_entry *fe;
    struct list_elem *e;
    if (*index == list_end(queue)) {
        *index = list_begin(queue);
        if (*index == list_end(queue)) {
            return NULL;
        }
    }
    while (true)
    {   
        bool rr = false;
        fe = list_entry(*index, struct frame_entry, l_elem);
        for (e = list_begin(&fe->owners);
             e != list_end(&fe->owners);
             e = list_next(e))
        {   
            struct owner *frame_owner = list_entry(e, struct owner, elem);
            if (frame_owner->t->pagedir)
            {
                rr |= 
                    pagedir_is_accessed(frame_owner->t->pagedir, frame_owner->upage);
                pagedir_set_accessed(frame_owner->t->pagedir, 
                                    frame_owner->upage,
                                    false);

            }
        }
        get_next(index, queue);
        // printf("index:%p\n", *index);
        if (!rr)
        {
            break;
        }
    }
    // printf("size return %d\n", fe->owners_list_size);
    return fe;

}

bool
free_frame(struct hash *frame_table,
           struct list *queue,
           void *kva, 
           struct list_elem **index)
{   
    struct frame_entry fake_frame;
    fake_frame.kva = kva;
    struct hash_elem *he = hash_delete(frame_table, &fake_frame.elem);
    if (he != NULL)
    {
        // mathced an entry
        struct frame_entry *fe = hash_entry(he, struct frame_entry, elem);
        if (*index == &fe->l_elem)
        {
            get_next(index, queue);
        }
        list_remove(&fe->l_elem);
        free(fe);
        return true;
    }
    return false;
}

struct frame_entry *find_frame_entry(struct hash *frame_table, void *kpage) {
    struct frame_entry fake_fentry;
    fake_fentry.kva = kpage;
    return hash_entry(hash_find(frame_table, &fake_fentry.elem), struct frame_entry, elem);
}
    

static unsigned frame_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    return ((unsigned) (hash_entry(e, struct frame_entry, elem) -> kva));
}

static bool frame_less_func (const struct hash_elem *a, 
                             const struct hash_elem *b, 
                             void *aux UNUSED)
{
    return (hash_entry(a, struct frame_entry, elem) -> kva) 
        < (hash_entry(b, struct frame_entry, elem) -> kva);
}

void destroy_frame_table(struct hash *frame_table)
{
    hash_destroy(frame_table, frame_destroy_func);
}

static void frame_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
    free(hash_entry(e, struct frame_entry, elem));
}

void
get_next(struct list_elem **index, struct list *queue_pt)
{   
    // edge case when tail at start
    if (list_end(queue_pt) == *index)
    {
        *index = list_begin(queue_pt);
        return;
    }
    
    // move the index along
    *index = list_next(*index);
    
    // check circular fashion
    if (list_end(queue_pt) == *index)
    {
        *index = list_begin(queue_pt);
    }
    ASSERT (*index);
}