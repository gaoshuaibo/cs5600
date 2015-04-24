#include <hash.h>
#include <stdio.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "devices/timer.h" 
#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "lib/string.h"
#define FLUSH_INTERVAL 10000
#define BLOCK_CACHE_SIZE (DISK_SECTOR_SIZE * 64);
#define RANDOM_FLUSH_NUM 10000000
#define BLOCK_CACHE_MAX 64
struct cached_block{
  uint32_t data[BLOCK_SECTOR_SIZE / 4];
  block_sector_t sector;
  struct hash_elem hash_elem;
  struct list_elem list_elem;
  bool dirty;
};

static struct cached_block *find_block(block_sector_t trgt);
struct list lru;
struct hash cache;
struct lock fs_lock;
struct lock hash_lock;
static bool hash_sector_order(const struct hash_elem *e1, const struct hash_elem *e2, void *AUX UNUSED){
  struct cached_block *cb1 = hash_entry(e1, struct cached_block, hash_elem);
  struct cached_block *cb2 = hash_entry(e2, struct cached_block, hash_elem);
  return cb1->sector < cb2->sector;
}
static unsigned hash_sector(const struct hash_elem *e1, void *AUX UNUSED){
  return hash_int(hash_entry(e1, struct cached_block, hash_elem)->sector);
}
static void hash_free(struct hash_elem *e, void* AUX UNUSED){
  struct cached_block *block = hash_entry(e, struct cached_block, hash_elem);
  if(block->dirty)
    block_write(fs_device, block->sector, block->data);
  //list_remove(&block->list_elem);
  free(block);

}

static void flush_cache(void){
  lock_acquire(&fs_lock);
  hash_clear(&cache, hash_free);
  lock_release(&fs_lock);

}

static void add_to_cache(struct cached_block *b){
  if(hash_size(&cache) == BLOCK_CACHE_MAX){
    flush_cache();
    /*struct list_elem *e = list_begin(&lru);
    ASSERT(e);
    struct cached_block *cb = list_entry(e, struct cached_block, list_elem);
    if(cb->dirty)
      block_write(fs_device, cb->sector, cb->data);
    hash_delete(&cache, &cb->hash_elem);
    list_remove(e);
    free(cb);
*/
  }
  lock_acquire(&fs_lock);
  hash_insert(&cache, &b->hash_elem);
  //list_push_back(&lru, &b->list_elem);
  lock_release(&fs_lock);

  //ASSERT(hash_size(&cache) <= BLOCK_CACHE_MAX);
}

static void periodically_flush_cache(void *args UNUSED){
  while(1) {
    flush_cache();
    timer_sleep(FLUSH_INTERVAL);
  }
}

void init_cache(void){
  
  lock_init(&fs_lock);
  list_init(&lru);
  hash_init(&cache, hash_sector, hash_sector_order, NULL);
  
  thread_create("Flusher", PRI_DEFAULT, periodically_flush_cache, NULL); 
}
static struct cached_block *find_block(block_sector_t trgt){
  struct cached_block *r = NULL;
  lock_acquire(&fs_lock);
  struct cached_block s;
  s.sector = trgt;

  struct hash_elem *e = hash_find(&cache, &s.hash_elem);

  if(!e)
    goto done;
  
  r = hash_entry(e, struct cached_block, hash_elem);
  //list_remove(&r->list_elem);
  //list_push_back(&lru, &r->list_elem);
  done:
    lock_release(&fs_lock);
    return r;
}
void cache_block_write(struct block *blks UNUSED, block_sector_t target, const void *buf){
 //block_write(blks, target, buf);
 struct cached_block *cb = find_block(target);
  if(!cb){
    cb = malloc(sizeof(struct cached_block));
    cb->sector = target;
    cb->dirty = true;
    add_to_cache(cb);
  }
  cb->dirty = true;
  lock_acquire(&fs_lock);
  memcpy(cb->data, buf, BLOCK_SECTOR_SIZE);
  lock_release(&fs_lock);
  //block_write(blks, target, buf);
}
void cache_block_read(struct block *blks, block_sector_t target, void *buf){
 struct cached_block *b = find_block(target);
  if(!b){
   block_read(blks, target, buf);
   b = malloc(sizeof(struct cached_block));
   b->sector = target;
   b->dirty = false;
   block_read(blks, target, b->data);
   add_to_cache(b);  
    return;
  }
  lock_acquire(&fs_lock);
  memcpy(buf, b->data, BLOCK_SECTOR_SIZE);
  lock_release(&fs_lock);
  //block_read(blks, target, buf);
  //block_read(blks, target, buf);
}



