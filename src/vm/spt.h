#ifndef SPT_H
#define SPT_H

#include "lib/kernel/hash.h"

enum data_location_flags
  {
    RAM,   
    SWAP_TABLE,      
    FILESYS,
    ALL_ZERO 
  };

struct spt_entry {
    void *upage;
	  void *data_pt;
    off_t absolute_off;
    size_t page_read_bytes;
    enum data_location_flags location;
    bool writable;
    struct hash_elem elem;
};

bool generate_spt_table(struct hash *spt_table);
void insert(struct hash *spt_table, struct spt_entry *spe);
void free_entry(struct hash *spt_table, void *upage);
void destroy_spt_table(struct hash *spt_table);

#endif SPT_H