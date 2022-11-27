#include "threads/malloc.h"
#include "lib/kernel/hash.h"
#include "spt.h"

static hash_hash_func spt_hash_func;  // hash function for frame table
static hash_less_func spt_less_func;  // hash less function for frame table
static hash_action_func spt_free_func;  // hash less function for frame table

bool 
generate_spt_table(struct hash *spt_table)
{
    return hash_init(spt_table, spt_hash_func, spt_less_func, NULL);
}

void 
insert_spe(struct hash *spt_table, struct spt_entry *spe)
{
    struct hash_elem *he = hash_insert(spt_table, &spe->elem);
    ASSERT(he);
}

bool contains_upage(struct hash *spt_table, void *upage)
{
    struct spt_entry fake_spe;
    fake_spe.upage = upage;
    return hash_find(spt_table, &fake_spe.elem) != NULL;
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

void 
destroy_spt_table(struct hash *spt_table)
{
    hash_destroy(spt_table, spt_free_func);
}

static void spt_free_func (struct hash_elem *e, void *aux UNUSED)
{
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