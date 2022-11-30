#ifndef FRAME_H
#define FRAME_H

#include "lib/kernel/hash.h"

struct frame_entry {
    uint32_t *kva;
    struct list owners;
    // struct thread *owner;
    struct hash_elem elem;
};

bool generate_frame_table(struct hash *frame_table);
void insert_frame(struct hash *frame_table, struct frame_entry *frame);
bool free_frame(struct hash *frame_table, void *kva);
struct frame_entry *evict_frame(struct hash *frame_table);

// TODO; think about frame table destruction memory leak

#endif /* vm/frame.h */