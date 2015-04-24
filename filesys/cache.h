#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

void init_cache(void);
void cache_block_read(struct block *blks, block_sector_t target, void *buf);
void cache_block_write(struct block *blks, block_sector_t target, const void *buf);

#endif 
