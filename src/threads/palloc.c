#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "devices/swap.h"
#include "vm/sharing.h"
#include "vm/spt.h"
#include "userprog/pagedir.h"

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct bitmap *used_map;            /* Bitmap of free pages. */
    uint8_t *base;                      /* Base of pool. */
  };

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

/* stroing meta data about the memory frames */
struct hash frame_table;

/* queue iterator for SECOND-CHANCE EVICTION */
struct list_elem *index;

/* queue for SECOND-CHANCE EVICTION ALGORITHM */
struct list queue;

/* stroing sharing data for files */
struct hash share_table;

/* synchronising frame table accesses */
struct lock frame_lock;

/* synchronising share table accesses */
struct lock share_lock;

/* Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
  printf("PALLOC ININTIT\n");
  /* Free memory starts at 1 MB and runs to the end of RAM. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;

  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");

  /* initialise the frame table */
  if (!generate_frame_table(&frame_table))
  {
    PANIC("Could not generate frame table! \n");
  }

  list_init(&queue);
  index = list_begin(&queue);

  /* initialise the frame table */
  if (!generate_sharing_table(&share_table))
  {
    PANIC("Could not generate sharing table! \n");
  }

  /* initialise the swap space */
  printf("INIT SWAPPING \n");

  lock_init(&share_lock);
  lock_init(&frame_lock);
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }

  return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags) 
{
  void *kpage =  palloc_get_multiple (flags, 1);

  if (flags & PAL_USER)
  { 
    bool prev_frame = re_lock_acquire(&frame_lock);
    if (kpage == NULL)
    {
      struct frame_entry *fe = evict_frame(&queue, &index);
      if (!fe)                                                  
      {
        re_lock_release(&frame_lock, prev_frame);
        if (flags & PAL_ASSERT)
        {
          PANIC ("PAL_ASSERT failed during page palloc! \n");  
        }
        return NULL;
      }

      /* signal for swapping/free, remove */
      ASSERT(fe->owners_list_size > 0);
      struct list_elem *e = list_begin(&fe->owners);
      ASSERT(e);
      struct owner *frame_owner = list_entry(e, struct owner, elem);
      bool prev_spt = re_lock_acquire(&frame_owner->t->spt_lock);
      struct spt_entry *spe 
          = find_spe(&frame_owner->t->sp_table, frame_owner->upage);
      ASSERT(spe != NULL);

      /* page can be swapped if dirty */
      if (pagedir_is_writable(frame_owner->t->pagedir, frame_owner->upage))
      {
        // hence not sharable
        if (pagedir_is_dirty(frame_owner->t->pagedir, frame_owner->upage))
        {
          // swap
          spe->location_prev = spe->location;
          spe->location = SWAP_SLOT;
          spe->swap_slot = swap_out(fe->kva);
          // printf("swap out: %u kpage bytes: %x\n", spe->swap_slot, * (int *)fe->kva);
        }
        /* reset frame_entry for new page */
        if (flags & PAL_ZERO)
        {
          memset(fe->kva, 0,PGSIZE); 
        }
        pagedir_clear_page(frame_owner->t->pagedir, frame_owner->upage);
        ASSERT(fe->owners_list_size == 1);

        re_lock_release(&frame_owner->t->spt_lock, prev_spt);
        list_remove(&frame_owner->elem);
        free(frame_owner);
        ASSERT(!fe->inner_entry);
        fe->owners_list_size = 0;
        re_lock_release(&frame_lock, prev_frame);

        if (!fe->kva && (flags & PAL_ASSERT))
        {
          PANIC ("PAL_ASSERT failed during page palloc! \n");  
        }
        return fe->kva;
      }
      
      // means page is either an all_zero page
      if (spe->location == ALL_ZERO)
      {
        /* reset frame_entry for new page */
        if (flags & PAL_ZERO)
        {
          memset(fe->kva, 0,PGSIZE); 
        }
        pagedir_clear_page(frame_owner->t->pagedir, frame_owner->upage);
        ASSERT(fe->owners_list_size == 1);

        re_lock_release(&frame_owner->t->spt_lock, prev_spt);
        
        list_remove(&frame_owner->elem);
        free(frame_owner);
        ASSERT(!fe->inner_entry);
        fe->owners_list_size = 0;
        re_lock_release(&frame_lock, prev_frame);
        return fe->kva;
      }

      // in the case of sharing - multiple owners
      ASSERT(spe->location == FILE_SYS)
      re_lock_release(&frame_owner->t->spt_lock, prev_spt);

      struct list_elem *temp;      
      for (; e != list_end (&fe->owners);)
      {
        temp = e;
        e = list_next(e);
        struct owner *o = list_entry(temp, struct owner, elem);

        /* reset frame_entry for new page */
        if (flags & PAL_ZERO)
        {
          memset(fe->kva, 0,PGSIZE); 
        }

        pagedir_clear_page(o->t->pagedir, o->upage);
        list_remove(temp);
        free(o);
        fe->owners_list_size = 0;
      }
      
      /* reset frame_entry for new page and remove sharing entry */
      bool prev_share = re_lock_acquire(&share_lock);
      ASSERT(fe->inner_entry);
      delete_sharing_frame(&share_table, fe->inner_entry);
      re_lock_release(&share_lock, prev_share);
      fe->inner_entry = NULL;
      fe->owners_list_size = 0;
      re_lock_release(&frame_lock, prev_frame);
      return fe->kva;
    } 

    /* insert new entry into page table */
    struct frame_entry *frame_pt  = malloc(sizeof(struct frame_entry));
    list_init (&frame_pt->owners);
    frame_pt->kva = kpage;
    frame_pt->owners_list_size = 0;
    frame_pt->inner_entry = NULL;
    insert_frame(&frame_table, &queue, frame_pt);
    re_lock_release(&frame_lock, prev_frame);
  }
  return kpage;
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif
  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page) 
{
  if (page_from_pool (&user_pool, page))
  {
    bool prev_frame = re_lock_acquire(&frame_lock);
    bool prev_share = re_lock_acquire(&share_lock);

    struct thread *t = thread_current();
    struct owner *owner_obj = NULL;
    struct frame_entry *kframe_entry = find_frame_entry(&frame_table, page);
    ASSERT(kframe_entry);

    struct list_elem *e;
    for (e = list_begin(&kframe_entry->owners); 
         e != list_end(&kframe_entry->owners);
         e = list_next(e))
    {    
        owner_obj = list_entry(e, struct owner, elem);
        if (owner_obj->t->tid == t->tid)
        {
          break;
        }
    }

    if (owner_obj)
    {
      list_remove(&owner_obj->elem);
      kframe_entry->owners_list_size--;
    }
    
    if (kframe_entry->owners_list_size == 0) { 
      if (kframe_entry->inner_entry) {
        ASSERT(delete_sharing_frame(&share_table, kframe_entry->inner_entry));

        ASSERT(owner_obj);
        free(owner_obj);
      }
      ASSERT(free_frame(&frame_table, &queue, page, &index));
      re_lock_release(&share_lock, prev_share);
      re_lock_release(&frame_lock, prev_frame);
    } else {
      if (t->pagedir)
      {
        pagedir_clear_page(t->pagedir, owner_obj->upage);
      }
      ASSERT(owner_obj);
      free(owner_obj);

      re_lock_release(&share_lock, prev_share);
      re_lock_release(&frame_lock, prev_frame);
      return;
    }
  }
  palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;

  printf ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);
 
  return page_no >= start_page && page_no < end_page;
}

void palloc_finish (void)
{
  destroy_frame_table(&frame_table);
  destroy_share_table(&share_table);
}