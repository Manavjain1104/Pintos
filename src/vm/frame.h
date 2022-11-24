#ifndef FRAME_H
#define FRAME_H

#include "lib/kernel/hash.h"

#define NUM_BUCKETS 1024

struct frame_entry {
    uint32_t *kva;
    // add user va
    // pointer to the kva of the structure that the spt fetched
    struct thread *owner;
    struct hash_elem elem;
};

bool generate_frame_table(struct hash *);
void insert(struct hash *frame_table, struct frame_entry *frame);
bool free_frame(struct hash *frame_table, void *kva);
struct frame_entry *evict_frame(struct hash *frame_table);
void update_entry (struct frame_entry *old, struct frame_entry *new);

#endif /* vm/frame.h */