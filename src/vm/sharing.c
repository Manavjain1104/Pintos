#include "sharing.h"
#include "lib/string.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"


static hash_hash_func sharing_hash_func;  // hash function for sharing table
static hash_less_func sharing_less_func;  // hash less function for sharing table
static hash_hash_func inner_sharing_hash_func;  // hash less function for sharing table
static hash_less_func inner_sharing_less_func;  // hash less function for sharing table

int primes[MAX_FILE_NAME_SIZE] = {2,3,5,7,11,13,17,19,23,29,31,37,41,43};

bool generate_sharing_table(struct hash *sharing_table) {
    return hash_init(sharing_table, sharing_hash_func, sharing_less_func, NULL);
}

static unsigned int sharing_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
   return hash_entry(e, struct outer_share_entry, elem)->hash_val;
   
}

unsigned calculate_hash_val(char *file_name)
{
   int sum = 0;
   for (unsigned i = 0; i < strlen(file_name); i++)
   {
    sum += (file_name[i] * primes[i]);
   }
   return sum;
}


static bool sharing_less_func (const struct hash_elem *a, 
                             const struct hash_elem *b, 
                             void *aux UNUSED)
{
    return sharing_hash_func(a,NULL)
        < sharing_hash_func(b,NULL);
}

void insert_sharing_entry(struct hash *sharing_table, char *file_name, unsigned page_num, void *kpage)
{
    struct outer_share_entry fake_entry;
    fake_entry.hash_val = calculate_hash_val(file_name);
    struct hash_elem *actual_elem = hash_find(sharing_table, &fake_entry.elem);
    struct hash inner_sharing_table;
    if (!actual_elem)
    {
        struct outer_share_entry *oshare_entry = malloc(sizeof(struct outer_share_entry));
        oshare_entry->file_name = file_name;
        oshare_entry->hash_val = fake_entry.hash_val;
        hash_init(&oshare_entry->inner_sharing_table, inner_sharing_hash_func, inner_sharing_less_func, NULL);
        actual_elem = &oshare_entry->elem;
        inner_sharing_table = oshare_entry->inner_sharing_table;
    } else
    {
        inner_sharing_table = hash_entry(actual_elem, struct outer_share_entry, elem)->inner_sharing_table;
    }
    struct inner_share_entry *ishare_entry = malloc(sizeof(struct inner_share_entry));
    ishare_entry->page_num = page_num;
    ishare_entry->kpage = kpage;
    ASSERT(!hash_insert(&inner_sharing_table, &ishare_entry->elem));
}

void *find_sharing_entry(struct hash *sharing_table, char *file_name, unsigned page_num) {
    struct outer_share_entry fake_entry;
    fake_entry.hash_val = calculate_hash_val(file_name);
    struct hash_elem *actual_elem = hash_find(sharing_table, &fake_entry.elem);
    if (!actual_elem) {
        return NULL;
    }
    struct inner_share_entry ifake_entry;
    ifake_entry.page_num = page_num;
    struct hash_elem *iactual_elem = hash_find(
        &hash_entry(actual_elem, struct outer_share_entry, elem)->inner_sharing_table,
        &ifake_entry.elem
    );
    if (!iactual_elem) {
        return NULL;
    }
    return hash_entry(iactual_elem, struct inner_share_entry, elem)->kpage;
}

bool delete_sharing_frame(struct hash *sharing_table, struct inner_share_entry *isentry)
{
    struct hash_elem *ishare_entry = hash_delete(sharing_table, &isentry->elem);
    if (!ishare_entry) {
        return false;
    }
    free(ishare_entry);
    return true;
}


static unsigned int inner_sharing_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    return hash_entry(e, struct inner_share_entry, elem)->page_num;
}

static bool inner_sharing_less_func(const struct hash_elem *a, 
                             const struct hash_elem *b, 
                             void *aux UNUSED)
{
    return inner_sharing_hash_func(a, NULL) < inner_sharing_hash_func(b, NULL);
}
