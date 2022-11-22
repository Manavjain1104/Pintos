#ifndef FRAME_H
#define FRAME_H

#include "lib/kernel/hash.h"

#define NUM_BUCKETS 1024

struct frame_entry {
    void *kva;
    struct thread *owner;
    struct hash_elem elem;
};

void generate_frame_table(struct hash *);
void insert(struct hash *frame_table, struct frame_entry *frame);
void free_frame(struct hash *frame_table, 
    struct frame_entry *fake_frame);
struct frame_entry *evict_frame(struct hash *frame_table);
void update_entry (struct frame_entry *old, struct frame_entry *new);
#endif /* vm/frame.h */