#include "threads/malloc.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "frame.h"

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
             struct frame_entry *frame,
             struct hash_iterator *it)
{
    struct hash_elem *he = hash_insert(frame_table, &frame->elem);
    ASSERT(!he);
    hash_first(it, frame_table);
}

struct frame_entry *
evict_frame(struct hash *frame_table, struct hash_iterator *it)
{
    struct hash_elem *he;
    struct frame_entry *fe;
    struct list_elem *e;
    while((he = get_next(it, frame_table)))
    {   
        bool rr = false;
        fe = hash_entry(he, struct frame_entry, elem);
        for (e = list_begin(&fe->owners);
             e != list_end(&fe->owners);
             e = list_next(e))
        {   
            struct owner *frame_owner = list_entry(e, struct owner, elem);
            // ASSERT(frame_owner->t->pagedir);
            if (frame_owner->t->pagedir)
            {
                bool booltocheck = pagedir_is_accessed(frame_owner->t->pagedir, frame_owner->upage);
                rr |= 
                    booltocheck;
                pagedir_set_accessed(frame_owner->t->pagedir, 
                                    frame_owner->upage,
                                    false);

            }
        }
        if (rr)
        {
            break;
        }
    }

    if (he)
    {
        return hash_entry(he, struct frame_entry, elem);
    }
    return NULL;
}

bool
free_frame(struct hash *frame_table, void *kva, struct hash_iterator *it)
{   
    struct frame_entry fake_frame;
    fake_frame.kva = kva;
    struct hash_elem *he = hash_delete(frame_table, &fake_frame.elem);
    hash_first(it, frame_table);
    if (he != NULL)
    {
        // mathced an entry
        struct frame_entry *fe = hash_entry(he, struct frame_entry, elem);
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

struct hash_elem *
get_next(struct hash_iterator *it, struct hash *frames)
{
    struct hash_elem *he = hash_next(it);
    if (he)
    {
        return he;
    }
    hash_first(it, frames);
    return hash_next(it);
}