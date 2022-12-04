#ifndef FRAME_H
#define FRAME_H

#include "lib/kernel/hash.h"

struct frame_entry {
    uint32_t *kva;
    struct list owners;
    unsigned owners_list_size;
    struct inner_share_entry *inner_entry;
    struct hash_elem elem;
    struct list_elem l_elem;
};

struct owner {
    struct thread *t;
    void *upage;
    struct list_elem elem;
};

bool generate_frame_table(struct hash *frame_table);
struct frame_entry *find_frame_entry(struct hash *frame_table, void *kva);
void insert_frame(struct hash *frame_table,
                  struct list *queue,
                  struct frame_entry *frame);
bool free_frame(struct hash *frame_table, 
                struct list *queue,
                void *kva, 
                struct list_elem **index);
struct frame_entry *evict_frame(struct list *queue,
                                struct list_elem **index);
void destroy_frame_table(struct hash *frame_table);
void get_next(struct list_elem **index, struct list *queue_pt);

#endif /* vm/frame.h */