#ifndef SHARING_H
#define SHARING_H

#include "lib/kernel/hash.h"

#define MAX_FILE_NAME_SIZE 14

struct outer_share_entry {
    char *file_name;
    unsigned int hash_val;
    unsigned int size;
    struct hash inner_sharing_table;
    struct hash_elem elem;
};

struct inner_share_entry {
    unsigned page_num;
    void *kpage;
    struct hash_elem elem;
    struct outer_share_entry *outer_entry;
};


unsigned calculate_hash_val(char *file_name);
bool generate_sharing_table(struct hash *sharing_table);
struct inner_share_entry * 
insert_sharing_entry(struct hash *sharing_table, 
                     char *file_name, 
                     unsigned page_num, 
                     void *kpage);
void *find_sharing_entry(struct hash *sharing_table, char *file_name, unsigned page_num);
bool delete_sharing_frame(struct hash *sharing_table, struct inner_share_entry *isentry);
void destroy_share_table(struct hash *share_table);

#endif /* vm/sharing.h */ 