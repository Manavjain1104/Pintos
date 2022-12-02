#ifndef FRAME_H
#define FRAME_H

#include "lib/kernel/hash.h"

struct frame_entry {
    bool reference_bit;   
    uint32_t *kva;
    struct list owners;
    unsigned owners_list_size;
    struct inner_share_entry *inner_entry;
    // reference bit of the frame - amalgation of access bits of all sharing upages
    struct hash_elem elem;
};

struct owner {
    struct thread *t;
    void *upage;
    struct list_elem elem;
};

bool generate_frame_table(struct hash *frame_table);
struct frame_entry *find_frame_entry(struct hash *frame_table, void *kva);
void insert_frame(struct hash *frame_table, 
                  struct frame_entry *frame, 
                  struct hash_iterator *it);
bool free_frame(struct hash *frame_table, void *kva, struct hash_iterator *it);
struct frame_entry *evict_frame(struct hash *frame_table, 
                                struct hash_iterator *it);
void destroy_frame_table(struct hash *frame_table);
struct hash_elem *get_next(struct hash_iterator *it, struct hash *frame_table);

// TODO; think about frame table destruction memory leak

#endif /* vm/frame.h */