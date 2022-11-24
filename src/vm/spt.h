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
    void *page;
	  void *table_pt;
    enum data_location_flags location;
    bool is_writable;
    bool is_readable;
    hash_elem hash_elem;
};


#endif SPT_H