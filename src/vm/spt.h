#ifndef SPT_H
#define SPT_H

#include "lib/kernel/hash.h"
#include "filesys/off_t.h"

enum data_location_flags
  {
    RAM,   
    SWAP_SLOT,      
    FILE_SYS,
    ALL_ZERO,
    STACK
  };

struct spt_entry {
    void *upage;    // key of the table
	  // void *data_pt;  // soruce of data

    off_t absolute_off;        // meta data for file sys 
    size_t page_read_bytes;    // loading

    size_t swap_slot;         // slot of swapped out page

    enum data_location_flags location; // location of data to be loaded
    
    bool writable; // writability of the page
    struct hash_elem elem; // to make part of spt
};

bool generate_spt_table(struct hash *spt_table);
void insert_spe(struct hash *spt_table, struct spt_entry *spe);
void free_entry(struct hash *spt_table, void *upage);
void destroy_spt_table(struct hash *spt_table);

#endif /* vm/spt.h */