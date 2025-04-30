// cache.c - Block cache for a storage device
//
// Copyright (c) 2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "cache.h"
#include "io.h"
#include "assert.h"
#include "error.h"
#include "memory.h"
#include "string.h"
#include "conf.h"
#include "heap.h"
#include <stddef.h>

// Cache block structure
struct cache_block {
    unsigned long long pos;     // Position in backing storage
    void *data;                 // Pointer to block data
    int dirty;                  // Dirty flag (CACHE_CLEAN or CACHE_DIRTY)
    int valid;                  // Valid flag (0 if invalid, 1 if valid)
    int refcount;               // Reference count
    int last_access;            // Last access timestamp for LRU algorithm
};

// Cache structure
struct cache {
    struct io *backing_io;      // Backing I/O device
    struct cache_block *blocks; // Array of cache blocks
    int block_count;            // Number of blocks in cache
    int access_time;            // Global timestamp for LRU algorithm
    int blksz;                  // Block size
};

/**
 * Create a new cache for the given backing I/O device
 * @param bkgio Backing I/O device
 * @param cptr Pointer to store cache pointer
 * @return 0 on success, error code on failure
 */
int create_cache(struct io *bkgio, struct cache **cptr) {
    struct cache *cache;
    int i;
    int blksz;
    
    // Check input parameters
    if (!bkgio || !cptr) {
        kprintf("create_cache: Invalid args (bkgio=%p, cptr=%p)\n", bkgio, cptr); //debug
        return EINVAL;
    }
    
    // Get block size from backing I/O
    if (ioctl(bkgio, IOCTL_GETBLKSZ, &blksz) != 1) {
        kprintf("create_cache: IOCTL_GETBLKSZ failed\n"); //debug
        return ENOTSUP;
    }
    
    // Ensure block size matches expected size
    if (blksz != CACHE_BLKSZ) {
        kprintf("create_cache: Block size mismatch (%d != %d)\n", blksz, CACHE_BLKSZ); //debug
        return EBADFMT;
    }
    
    // Allocate cache structure
    cache = kmalloc(sizeof(struct cache));
    if (!cache) {
        kprintf("create_cache: Failed to allocate cache\n"); //debug
        return ENOMEM;
    }
    
    // Initialize cache structure
    cache->backing_io = ioaddref(bkgio);
    cache->block_count = CACHE_CAPACITY;
    cache->access_time = 0;
    cache->blksz = blksz;
    
    // Allocate cache blocks
    cache->blocks = kmalloc(sizeof(struct cache_block) * CACHE_CAPACITY);
    if (!cache->blocks) {
        ioclose(cache->backing_io);
        kfree(cache);
        return ENOMEM;
    }
    
    // Initialize cache blocks
    for (i = 0; i < CACHE_CAPACITY; i++) {
        cache->blocks[i].pos = 0;
        cache->blocks[i].dirty = CACHE_CLEAN;
        cache->blocks[i].valid = 0;
        cache->blocks[i].refcount = 0;
        cache->blocks[i].last_access = 0;
        
        // Allocate block data
        cache->blocks[i].data = kmalloc(blksz);
        if (!cache->blocks[i].data) {
            // Free already allocated blocks
            while (--i >= 0) {
                kfree(cache->blocks[i].data);
            }
            kfree(cache->blocks);
            ioclose(cache->backing_io);
            kfree(cache);
            return ENOMEM;
        }
    }
    
    // Return cache pointer
    *cptr = cache;
    return 0;
}

/**
 * Find a cache block by position
 * @param cache Cache
 * @param pos Position in backing storage
 * @return Index of block, or -1 if not found
 */
static int find_block(struct cache *cache, unsigned long long pos) {
    int i;
    for (i = 0; i < cache->block_count; i++) {
        if (cache->blocks[i].valid && cache->blocks[i].pos == pos) {
            return i;
        }
    }
    return -1;
}

/**
 * Find a free or least recently used block
 * @param cache Cache
 * @return Index of block
 */
static int find_free_or_lru_block(struct cache *cache) {
    int i;
    int lru_index = -1;
    int min_access = cache->access_time + 1;
    
    // First, try to find an invalid block
    for (i = 0; i < cache->block_count; i++) {
        if (!cache->blocks[i].valid && cache->blocks[i].refcount == 0) {
            return i;
        }
    }
    
    // Otherwise, find the least recently used block that's not referenced
    for (i = 0; i < cache->block_count; i++) {
        if (cache->blocks[i].refcount == 0 && cache->blocks[i].last_access < min_access) {
            min_access = cache->blocks[i].last_access;
            lru_index = i;
        }
    }
    
    return lru_index;
}

/**
 * Get a block from the cache
 * @param cache Cache
 * @param pos Position in backing storage
 * @param pptr Pointer to store block data pointer
 * @return 0 on success, error code on failure
 */




int cache_get_block(struct cache *cache, unsigned long long pos, void **pptr) {
    int block_index;
    long read_size;
    
    kprintf("cache_get_block: pos=%llu\n", pos);
    if (!cache || !pptr) {
        kprintf("cache_get_block: Invalid args\n");
        return -EINVAL;
    }
    
    pos = (pos / cache->blksz) * cache->blksz;
    
    block_index = find_block(cache, pos);
    kprintf("cache_get_block: block_index=%d\n", block_index);
    
    if (block_index < 0) {
        block_index = find_free_or_lru_block(cache);
        kprintf("cache_get_block: free_or_lru block_index=%d\n", block_index);
        if (block_index < 0) {
            kprintf("cache_get_block: No free or LRU block\n");
            return -EBUSY;
        }
        
        if (cache->blocks[block_index].valid && cache->blocks[block_index].dirty) {
            long write_size = iowriteat(cache->backing_io, 
                                       cache->blocks[block_index].pos,
                                       cache->blocks[block_index].data, 
                                       cache->blksz);
            kprintf("cache_get_block: write_size=%ld\n", write_size);
            if (write_size != cache->blksz) {
                kprintf("cache_get_block: Write failed\n");
                return -EIO;
            }
        }
        
        kprintf("cache_get_block: block data ptr=%p\n", cache->blocks[block_index].data);
        if (!cache->blocks[block_index].data) {
            kprintf("cache_get_block: Block data pointer is NULL\n");
            return -EIO;
        }
        
        read_size = ioreadat(cache->backing_io, pos, cache->blocks[block_index].data, cache->blksz);
        kprintf("ioreadat: pos=%llu, read_size=%ld\n", pos, read_size);
        
        if (read_size < 0) {
            kprintf("cache_get_block: Read failed\n");
            return -EIO;
        }
        if (read_size < cache->blksz) {
            unsigned long long end_pos;
            if (ioctl(cache->backing_io, IOCTL_GETEND, &end_pos) == 0 && pos < end_pos) {
                kprintf("cache_get_block: Partial read, end_pos=%llu\n", end_pos);
                if (read_size >= 0) {
                    memset((char*)cache->blocks[block_index].data + read_size, 0, cache->blksz - read_size);
                } else {
                    memset(cache->blocks[block_index].data, 0, cache->blksz);
                }
            } else {
                kprintf("cache_get_block: Partial read outside device bounds\n");
                return -EIO;
            }
        }
        
        cache->blocks[block_index].pos = pos;
        cache->blocks[block_index].dirty = CACHE_CLEAN;
        cache->blocks[block_index].valid = 1;
    }
    
    cache->blocks[block_index].last_access = ++cache->access_time;
    cache->blocks[block_index].refcount++;
    
    *pptr = cache->blocks[block_index].data;
    kprintf("cache_get_block: *pptr=%p\n", *pptr);
    if (*pptr == NULL) {
        kprintf("cache_get_block: Block pointer is NULL\n");
        return -EIO;
    }
    return 0;
}

/**
 * Release a block from the cache
 * @param cache Cache
 * @param pblk Pointer to block data
 * @param dirty Dirty flag (CACHE_CLEAN or CACHE_DIRTY)
 */
void cache_release_block(struct cache *cache, void *pblk, int dirty) {
    int i;
    
    // Check input parameters
    if (!cache || !pblk) {
        return;
    }
    
    // Find block in cache
    for (i = 0; i < cache->block_count; i++) {
        if (cache->blocks[i].data == pblk) {
            // Update dirty flag if needed
            if (dirty == CACHE_DIRTY) {
                cache->blocks[i].dirty = CACHE_DIRTY;
            }
            
            // Decrement reference count
            if (cache->blocks[i].refcount > 0) {
                cache->blocks[i].refcount--;
            }
            
            return;
        }
    }
    
    // Block not found, should not happen
    panic("cache_release_block: block not found");
}

/**
 * Flush all dirty blocks to backing storage
 * @param cache Cache
 * @return 0 on success, error code on failure
 */
int cache_flush(struct cache *cache) {
    int i;
    long write_size;
    
    // Check input parameters
    if (!cache) {
        return EINVAL;
    }
    
    // Flush all dirty blocks
    for (i = 0; i < cache->block_count; i++) {
        if (cache->blocks[i].valid && cache->blocks[i].dirty) {
            write_size = iowriteat(cache->backing_io, 
                                  cache->blocks[i].pos, 
                                  cache->blocks[i].data, 
                                  cache->blksz);
            if (write_size != cache->blksz) {
                return EIO;
            }
            
            // Mark block as clean
            cache->blocks[i].dirty = CACHE_CLEAN;
        }
    }
    
    return 0;
}