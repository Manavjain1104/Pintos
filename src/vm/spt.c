#include "threads/malloc.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "devices/swap.h"
#include "spt.h"

static hash_hash_func spt_hash_func;  // hash function for frame table
static hash_less_func spt_less_func;  // hash less function for frame table
static hash_action_func spt_destroy_func;  // hash less function for frame table

bool 
generate_spt_table(struct hash *spt_table)
{
    return hash_init(spt_table, spt_hash_func, spt_less_func, NULL);
}

struct hash_elem * 
insert_spe(struct hash *spt_table, struct spt_entry *spe)
{
    struct hash_elem *he = hash_insert(spt_table, &spe->elem);
    return he;
}

void 
free_entry(struct hash *spt_table, void *upage)
{
    struct spt_entry fake_spe;
    fake_spe.upage = upage;
    struct hash_elem *he = hash_delete(spt_table, &fake_spe.elem);
    ASSERT(he);
    free(hash_entry(he, struct spt_entry, elem));
}

void update_spe(struct spt_entry *old_spe, struct spt_entry *new_spe)
{
    old_spe->writable = new_spe->writable;
    old_spe->page_read_bytes = new_spe->page_read_bytes;
    old_spe->absolute_off = new_spe->absolute_off;
    old_spe->location = new_spe->location;
}

void 
destroy_spt_table(struct hash *spt_table)
{
    hash_destroy(spt_table, spt_destroy_func);
}

static void spt_destroy_func (struct hash_elem *e, void *aux UNUSED)
{   
    struct spt_entry *spe = hash_entry(e, struct spt_entry, elem);
    if (spe->location == SWAP_SLOT)
    {   
        void *fake_page = palloc_get_page(PAL_ASSERT | PAL_ZERO);
        swap_in (fake_page, spe->swap_slot);
        palloc_free_page(fake_page); 
    }
    free(hash_entry(e, struct spt_entry, elem));
}

static unsigned spt_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    return ((unsigned) 
        (hash_entry(e, struct spt_entry, elem) -> upage));
}

static bool spt_less_func (const struct hash_elem *a, 
                             const struct hash_elem *b, 
                             void *aux UNUSED)
{
    return (hash_entry(a, struct spt_entry, elem) -> upage) 
        < (hash_entry(b, struct spt_entry, elem) -> upage);
}