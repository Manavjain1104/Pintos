#include "threads/malloc.h"
#include "lib/kernel/hash.h"
#include "frame.h"

static hash_hash_func frame_hash_func;  // hash function for frame table
static hash_less_func frame_less_func;  // hash less function for frame table


void
generate_frame_table(struct hash *frame_table) 
{
    return (hash_init(frame_table, frame_hash_func, frame_less_func, NULL));
}

void 
insert(struct hash *frame_table, struct frame_entry *frame)
{
    struct hash_elem *he = hash_insert(frame_table, &frame->elem);
    if (he != NULL)
    {   
        /* update values in the old_entry with the same frame kva */
        update_entry (hash_entry(he, struct frame_entry, elem), frame);
        free(frame);
    }
}

struct frame_entry *
evict_frame(struct hash *frame_table)
{
    // implement eviction policy - AUKAAT BANAO PEHLE LAUDE
    return NULL;
}

void
free_frame(struct hash *frame_table, struct frame_entry *fake_frame)
{
    struct hash_elem *he = hash_delete(frame_table, fake_frame);
    if (he != NULL)
    {
        // mathced an entry
        free(hash_entry(he, struct frame_entry, elem));
    }
}
    

static unsigned frame_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    return ((unsigned) (hash_entry(e, struct frame_entry, elem) -> kva)) % NUM_BUCKETS;
}

static bool frame_less_func (const struct hash_elem *a, 
                             const struct hash_elem *b, 
                             void *aux UNUSED)
{
    return (hash_entry(a, struct frame_entry, elem) -> kva) 
        < (hash_entry(b, struct frame_entry, elem) -> kva);
}

void update_entry (struct frame_entry *old, struct frame_entry *new)
{
    old->owner = new->owner;
}
