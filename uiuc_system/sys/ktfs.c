// ktfs.c - KTFS implementation
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "heap.h"
#include "fs.h"
#include "ioimpl.h"
#include "ktfs.h"
#include "error.h"
#include "thread.h"
#include "string.h"  // Reverted to original include, as <string.h> didn't resolve memmove
#include "console.h"
#include "cache.h"
#include <stdint.h>  // Add at the top with other includes

// INTERNAL TYPE DEFINITIONS
//

struct ktfs_file {
    // Fill to fulfill spec
    struct io io;                   // io struct
    uint32_t size;                  // file size 
    struct ktfs_dir_entry dentry;   // directory entry
    uint32_t flags;                 // flags
    struct ktfs_inode inode;        // inode for this file
    uint16_t inode_num;             // inode number
    struct io *seekable_io;         // pointer to seekable io
};

// INTERNAL DATA
//

#define MAX_FILES_OPEN 10

static struct ktfs_file my_files[MAX_FILES_OPEN];
static int how_many_files_open = 0;     // files open
static struct io *my_device = NULL;     // device read from
static struct cache *my_cache = NULL;   // cache for speeeeed
static struct ktfs_superblock my_superblock; // to know if file system is set up
static uint32_t where_inodes_start;     // where inodes start
static uint32_t where_data_starts;      // where data starts  

// INTERNAL FUNCTION DECLARATIONS
//

int ktfs_mount(struct io *io);
int ktfs_open(const char *name, struct io **ioptr);
void ktfs_close(struct io *io);
long ktfs_readat(struct io *io, unsigned long long pos, void *buf, long len);
int ktfs_cntl(struct io *io, int cmd, void *arg);
long ktfs_writeat(struct io *io, unsigned long long pos, const void *buf, long len);
int ktfs_create(const char *name);
int ktfs_delete(const char *name);

int ktfs_getblksz(struct ktfs_file *fd);
int ktfs_getend(struct ktfs_file *fd, void *arg);

int ktfs_flush(void);

// FUNCTION ALIASES
//

int fsmount(struct io *io)
    __attribute__ ((alias("ktfs_mount")));

int fsopen(const char *name, struct io **ioptr)
    __attribute__ ((alias("ktfs_open")));

int fsflush(void)
    __attribute__ ((alias("ktfs_flush")));

// internal functions
//

// helper function to find ktfs_file from io_pointer
static struct ktfs_file *find_file_from_io(struct io *io) {
    // look thru all files
    for (int i = 0; i < MAX_FILES_OPEN; i++) {
        if (my_files[i].flags == KTFS_FILE_IN_USE) {
            if (&my_files[i].io == io || my_files[i].seekable_io == io) {
                return &my_files[i];
            }
        }
    }
    kprintf("find_file_from_io: No file found for io=%p\n", io);
    return NULL; // find fail 
}

static int read_a_block(uint32_t block_num, void *buf) {
    void *block_pointer;
    int result;

    kprintf("read_a_block: ENTER - block_num=%u, buf=%p\n", block_num, buf);
    
    if (!my_cache) {
        kprintf("read_a_block: my_cache is NULL\n");  
        return -EINVAL;
    }

    kprintf("read_a_block: Calculating block position - block_num=%u, KTFS_BLKSZ=%u\n", block_num, KTFS_BLKSZ);
    uint32_t block_pos = block_num * KTFS_BLKSZ;
    kprintf("read_a_block: Block position = %u\n", block_pos);

    // get block from cache
    result = cache_get_block(my_cache, block_pos, &block_pointer);
    if (result < 0) {
        kprintf("read_a_block: cache_get_block failed for block %u (pos %u): %d\n", block_num, block_pos, result);
        return result;
    }

    if (!block_pointer) {
        kprintf("read_a_block: cache_get_block returned NULL for block %u (pos %u)\n", block_num, block_pos);
        return -EIO;
    }

    kprintf("read_a_block: Got block pointer %p, copying %u bytes\n", block_pointer, KTFS_BLKSZ);
    
    // Print first 32 bytes of source data before copy
    kprintf("read_a_block: First 32 bytes of source data:\n");
    for (int i = 0; i < 32; i++) {
        kprintf("  [%d] = 0x%02x\n", i, ((unsigned char *)block_pointer)[i]);
    }
    
    // copy data from cache to buffer
    memcpy(buf, block_pointer, KTFS_BLKSZ);  // Faster and safer than loop

    // Print first 32 bytes of destination data after copy
    kprintf("read_a_block: First 32 bytes of destination data:\n");
    for (int i = 0; i < 32; i++) {
        kprintf("  [%d] = 0x%02x\n", i, ((unsigned char *)buf)[i]);
    }

    cache_release_block(my_cache, block_pointer, 0);
    kprintf("read_a_block: Successfully read block %u\n", block_num);
    return 0;
}

static int write_a_block(uint32_t block_num, const void *buf) {
    void *block_pointer;
    int result;

    if (!my_cache) {
        kprintf("write_a_block: my_cache is NULL\n");
        return -EINVAL;
    }

    result = cache_get_block(my_cache, block_num * KTFS_BLKSZ, &block_pointer);
    if (result < 0) {
        kprintf("write_a_block: cache_get_block failed for block %u: %d\n", block_num, result);
        return result;
    }

    if (!block_pointer) {
        kprintf("write_a_block: cache_get_block returned NULL for block %u\n", block_num);
        return -EIO;
    }

    // Ensure the block is properly initialized
    memset(block_pointer, 0, KTFS_BLKSZ);
    memcpy(block_pointer, buf, KTFS_BLKSZ);
    
    cache_release_block(my_cache, block_pointer, CACHE_DIRTY);
    kprintf("write_a_block: Wrote block %u\n", block_num);
    return 0;
}

static int figure_out_block(struct ktfs_inode *inode, unsigned long long pos, uint32_t *block_num) {
    // figure out what block we need based on pos
    uint32_t block_idx = pos / KTFS_BLKSZ;
    int direct_blocks = KTFS_NUM_DIRECT_DATA_BLOCKS;
    int indirect_blocks = KTFS_BLKSZ / 4;
    int dindirect_blocks = indirect_blocks * indirect_blocks;
    struct ktfs_data_block indirect_block, dindirect_block;
    int result;

    kprintf("figure_out_block: ENTER - pos=%llu, block_idx=%u\n", pos, block_idx);
    kprintf("figure_out_block: Limits - direct=%d, indirect=%d, dindirect=%d\n", 
            direct_blocks, indirect_blocks, dindirect_blocks);
    kprintf("figure_out_block: Inode direct blocks: [%u, %u, %u]\n", 
            inode->block[0], inode->block[1], inode->block[2]);
    kprintf("figure_out_block: Inode indirect=%u\n", inode->indirect);

    // Validate block index is within file bounds
    if (block_idx * KTFS_BLKSZ >= inode->size) {
        kprintf("figure_out_block: Block index %u exceeds file size %u\n", block_idx, inode->size);
        return -EINVAL;
    }

    // check direct blocks
    if (block_idx < direct_blocks) {
        *block_num = inode->block[block_idx];
        kprintf("figure_out_block: Direct block[%u] = %u\n", block_idx, *block_num);
        if (*block_num == 0) {
            kprintf("figure_out_block: Direct block %u is 0\n", block_idx);
            return -EINVAL; // block not allocated
        }
        if (*block_num >= my_superblock.block_count) {
            kprintf("figure_out_block: Direct block %u exceeds total blocks %u\n", *block_num, my_superblock.block_count);
            return -EINVAL;
        }
        kprintf("figure_out_block: Found direct block %u at index %u\n", *block_num, block_idx);
        return 0;
    }
    block_idx -= direct_blocks;
    kprintf("figure_out_block: After direct blocks, block_idx=%u\n", block_idx);

    // check indirect
    if (block_idx < indirect_blocks) {
        kprintf("figure_out_block: Checking indirect block, idx=%u\n", block_idx);
        kprintf("figure_out_block: Inode indirect block number: %u\n", inode->indirect);
        kprintf("figure_out_block: Inode details - size: %llu, block: [%u, %u, %u]\n",
                inode->size, inode->block[0], inode->block[1], inode->block[2]);
        
        // Calculate the actual index into the indirect block
        uint32_t indirect_index = block_idx - direct_blocks;
        kprintf("figure_out_block: Indirect block calculation:\n");
        kprintf("  - Original block_idx: %u\n", block_idx);
        kprintf("  - Direct blocks: %u\n", direct_blocks);
        kprintf("  - Indirect index: %u\n", indirect_index);
        
        if (inode->indirect == 0) {
            kprintf("figure_out_block: Indirect block not allocated\n");
            return -EINVAL;
        }
        if (inode->indirect >= my_superblock.block_count) {
            kprintf("figure_out_block: Indirect block %u exceeds total blocks %u\n", inode->indirect, my_superblock.block_count);
            return -EINVAL;
        }
        
        kprintf("figure_out_block: Reading indirect block %u\n", inode->indirect);
        result = read_a_block(inode->indirect, &indirect_block);
        if (result < 0) {
            kprintf("figure_out_block: Failed to read indirect block %u: %d\n", inode->indirect, result);
            return result;
        }
        
        // Print both interpreted block numbers and raw data
        uint32_t *block_numbers = (uint32_t *)indirect_block.data;
        kprintf("figure_out_block: Indirect block data at index %u:\n", indirect_index);
        for (int i = 0; i < 8; i++) {
            kprintf("  [%d] = %u (0x%x)\n", i, block_numbers[i], block_numbers[i]);
        }
        kprintf("figure_out_block: Raw data from indirect block %u (first 64 bytes):\n", inode->indirect);
        for (int i = 0; i < 64; i += 4) {
            kprintf("  [%02d-%02d] = 0x%02x%02x%02x%02x (%c%c%c%c)\n", 
                   i, i+3,
                   indirect_block.data[i], indirect_block.data[i+1], 
                   indirect_block.data[i+2], indirect_block.data[i+3],
                   (indirect_block.data[i] >= 32 && indirect_block.data[i] <= 126) ? indirect_block.data[i] : '.',
                   (indirect_block.data[i+1] >= 32 && indirect_block.data[i+1] <= 126) ? indirect_block.data[i+1] : '.',
                   (indirect_block.data[i+2] >= 32 && indirect_block.data[i+2] <= 126) ? indirect_block.data[i+2] : '.',
                   (indirect_block.data[i+3] >= 32 && indirect_block.data[i+3] <= 126) ? indirect_block.data[i+3] : '.');
        }
        *block_num = block_numbers[block_idx];
        kprintf("figure_out_block: Indirect block[%u] = %u (0x%x)\n", block_idx, *block_num, *block_num);
        
        // Validate the block number
        if (*block_num == 0) {
            kprintf("figure_out_block: Indirect block %u at index %u is 0\n", inode->indirect, block_idx);
            return -EINVAL;
        }
        if (*block_num >= my_superblock.block_count) {
            kprintf("figure_out_block: Indirect block %u exceeds total blocks %u\n", *block_num, my_superblock.block_count);
            return -EINVAL;
        }
        // Additional validation: check if block number is within data block range
        if (*block_num < where_data_starts) {
            kprintf("figure_out_block: Indirect block %u is before data blocks start at %u\n", *block_num, where_data_starts);
            return -EINVAL;
        }
        kprintf("figure_out_block: Found valid indirect block %u at index %u\n", *block_num, block_idx);
        return 0;
    }
    block_idx -= indirect_blocks;
    kprintf("figure_out_block: After indirect blocks, block_idx=%u\n", block_idx);

    // check doubly indirect
    if (block_idx < dindirect_blocks * KTFS_NUM_DINDIRECT_BLOCKS) {
        int dindirect_idx = block_idx / dindirect_blocks;
        block_idx %= dindirect_blocks;
        int indirect_idx = block_idx / indirect_blocks;
        block_idx %= indirect_blocks;

        kprintf("figure_out_block: Checking doubly-indirect block:\n");
        kprintf("  - dindirect_idx=%d\n", dindirect_idx);
        kprintf("  - indirect_idx=%d\n", indirect_idx);
        kprintf("  - final block_idx=%u\n", block_idx);
        kprintf("  - dindirect block number=%u\n", inode->dindirect[dindirect_idx]);

        if (inode->dindirect[dindirect_idx] == 0) {
            kprintf("figure_out_block: Doubly-indirect block not allocated\n");
            return -EINVAL;
        }
        if (inode->dindirect[dindirect_idx] >= my_superblock.block_count) {
            kprintf("figure_out_block: Doubly-indirect block %u exceeds total blocks %u\n", 
                    inode->dindirect[dindirect_idx], my_superblock.block_count);
            return -EINVAL;
        }

        // Read the doubly-indirect block
        result = read_a_block(inode->dindirect[dindirect_idx], &dindirect_block);
        if (result < 0) {
            kprintf("figure_out_block: Failed to read doubly-indirect block %u: %d\n", 
                    inode->dindirect[dindirect_idx], result);
            return result;
        }

        // Get the indirect block number from the doubly-indirect block
        uint32_t *dindirect_block_numbers = (uint32_t *)dindirect_block.data;
        uint32_t indirect_block_num = dindirect_block_numbers[indirect_idx];
        kprintf("figure_out_block: Doubly-indirect block[%d] = %u\n", indirect_idx, indirect_block_num);

        if (indirect_block_num == 0) {
            kprintf("figure_out_block: Indirect block not allocated in doubly-indirect block\n");
            return -EINVAL;
        }
        if (indirect_block_num >= my_superblock.block_count) {
            kprintf("figure_out_block: Indirect block %u exceeds total blocks %u\n", 
                    indirect_block_num, my_superblock.block_count);
            return -EINVAL;
        }
        if (indirect_block_num < where_data_starts) {
            kprintf("figure_out_block: Indirect block %u is before data blocks start at %u\n", 
                    indirect_block_num, where_data_starts);
            return -EINVAL;
        }

        // Read the indirect block
        result = read_a_block(indirect_block_num, &indirect_block);
        if (result < 0) {
            kprintf("figure_out_block: Failed to read indirect block %u: %d\n", indirect_block_num, result);
            return result;
        }

        // Get the final block number
        uint32_t *block_numbers = (uint32_t *)indirect_block.data;
        *block_num = block_numbers[block_idx];
        kprintf("figure_out_block: Final block[%u] = %u\n", block_idx, *block_num);

        if (*block_num == 0) {
            kprintf("figure_out_block: Final block not allocated\n");
            return -EINVAL;
        }
        if (*block_num >= my_superblock.block_count) {
            kprintf("figure_out_block: Final block %u exceeds total blocks %u\n", 
                    *block_num, my_superblock.block_count);
            return -EINVAL;
        }
        if (*block_num < where_data_starts) {
            kprintf("figure_out_block: Final block %u is before data blocks start at %u\n", 
                    *block_num, where_data_starts);
            return -EINVAL;
        }

        kprintf("figure_out_block: Found doubly-indirect block %u\n", *block_num);
        return 0;
    }

    kprintf("figure_out_block: Block index %u exceeds maximum allowed\n", block_idx);
    return -EINVAL;
}

static int find_file_inode(const char *name, uint16_t *inode_num) {
    struct ktfs_inode root_inode;
    struct ktfs_data_block dir_block;
    struct ktfs_dir_entry *dentry;
    uint32_t num_entries;
    int result;

    int inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    uint32_t root_block = where_inodes_start + (my_superblock.root_directory_inode / inodes_per_block);
    uint32_t root_offset = (my_superblock.root_directory_inode % inodes_per_block) * KTFS_INOSZ;
    kprintf("Root inode block: %u, offset: %u, root_inode_idx: %u\n", 
            root_block, root_offset, my_superblock.root_directory_inode);

    struct ktfs_data_block inode_block;
    result = read_a_block(root_block, &inode_block);
    if (result < 0) {
        kprintf("find_file_inode: Failed to read inode block %u: %d\n", root_block, result);
        return result;
    }
    
    memcpy(&root_inode, inode_block.data + root_offset, sizeof(struct ktfs_inode));
    kprintf("Root inode size: %u, block[0]: %u\n", root_inode.size, root_inode.block[0]);

    num_entries = root_inode.size / KTFS_DENSZ;
    if (num_entries == 0) {
        kprintf("Root directory is empty (size=%u)\n", root_inode.size);
        return -ENOENT;
    }

    // Check all possible blocks in the root inode
    for (int i = 0; i < KTFS_NUM_DIRECT_DATA_BLOCKS; i++) {
        if (root_inode.block[i] == 0) {
            continue;  // Skip unallocated blocks
        }
        kprintf("find_file_inode: Checking direct block %u\n", root_inode.block[i]);

        result = read_a_block(root_inode.block[i], &dir_block);
        if (result < 0) {
            kprintf("find_file_inode: Failed to read data block %u: %d\n", root_inode.block[i], result);
            continue;
        }

        // Calculate how many entries are in this block
        uint32_t entries_in_block = KTFS_BLKSZ / KTFS_DENSZ;
        kprintf("find_file_inode: Block %u has %u entries\n", root_inode.block[i], entries_in_block);

        for (uint32_t j = 0; j < entries_in_block; j++) {
            dentry = (struct ktfs_dir_entry *)(dir_block.data + j * KTFS_DENSZ);
            kprintf("Entry %d: inode=%u, name='%s'\n", i * entries_in_block + j, dentry->inode, dentry->name);
            if (strncmp(dentry->name, name, KTFS_MAX_FILENAME_LEN) == 0) {
                *inode_num = dentry->inode;
                kprintf("Found file '%s' with inode %u\n", name, *inode_num);
                return 0;
            }
        }
    }

    // Check indirect block if it exists
    if (root_inode.indirect != 0) {
        struct ktfs_data_block indirect_block;
        result = read_a_block(root_inode.indirect, &indirect_block);
        if (result < 0) {
            kprintf("find_file_inode: Failed to read indirect block %u: %d\n", root_inode.indirect, result);
            return result;
        }

        uint32_t *block_numbers = (uint32_t *)indirect_block.data;
        for (int i = 0; i < KTFS_BLKSZ / sizeof(uint32_t); i++) {
            if (block_numbers[i] == 0) {
                continue;  // Skip unallocated blocks
            }
            kprintf("find_file_inode: Checking indirect block %u\n", block_numbers[i]);

            result = read_a_block(block_numbers[i], &dir_block);
            if (result < 0) {
                kprintf("find_file_inode: Failed to read data block %u: %d\n", block_numbers[i], result);
                continue;
            }

            uint32_t entries_in_block = KTFS_BLKSZ / KTFS_DENSZ;
            kprintf("find_file_inode: Block %u has %u entries\n", block_numbers[i], entries_in_block);

            for (uint32_t j = 0; j < entries_in_block; j++) {
                dentry = (struct ktfs_dir_entry *)(dir_block.data + j * KTFS_DENSZ);
                kprintf("Entry %d: inode=%u, name='%s'\n", 
                       KTFS_NUM_DIRECT_DATA_BLOCKS * entries_in_block + i * entries_in_block + j, 
                       dentry->inode, dentry->name);
                if (strncmp(dentry->name, name, KTFS_MAX_FILENAME_LEN) == 0) {
                    *inode_num = dentry->inode;
                    kprintf("Found file '%s' with inode %u\n", name, *inode_num);
                    return 0;
                }
            }
        }
    }

    kprintf("File '%s' not found in any block\n", name);
    return -ENOENT;
}

// New helper functions for block and inode management
static int allocate_block(uint32_t *block_num) {
    struct ktfs_bitmap bitmap;
    uint32_t bitmap_block_count = my_superblock.bitmap_block_count;
    uint32_t total_blocks = my_superblock.block_count;
    int result;

    kprintf("allocate_block: Searching for free block\n");
    for (uint32_t i = 0; i < bitmap_block_count; i++) {
        result = read_a_block(1 + i, &bitmap);
        if (result < 0) {
            kprintf("allocate_block: Failed to read bitmap block %u: %d\n", 1 + i, result);
            return result;
        }

        for (uint32_t byte = 0; byte < KTFS_BLKSZ; byte++) {
            if (bitmap.bytes[byte] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap.bytes[byte] & (1 << bit))) {
                        uint32_t block = (i * KTFS_BLKSZ * 8) + (byte * 8) + bit;
                        if (block >= total_blocks) {
                            kprintf("allocate_block: Block %u exceeds total blocks %u\n", block, total_blocks);
                            return -ENODATABLKS;
                        }
                        bitmap.bytes[byte] |= (1 << bit);
                        result = write_a_block(1 + i, &bitmap);
                        if (result < 0) {
                            kprintf("allocate_block: Failed to write bitmap block %u: %d\n", 1 + i, result);
                            return result;
                        }
                        *block_num = block;
                        kprintf("allocate_block: Allocated block %u\n", block);
                        return 0;
                    }
                }
            }
        }
    }

    kprintf("allocate_block: No free blocks available\n");
    return -ENODATABLKS;
}

static int free_block(uint32_t block_num) {
    struct ktfs_bitmap bitmap;
    uint32_t bitmap_block = 1 + (block_num / (KTFS_BLKSZ * 8));
    uint32_t byte = (block_num % (KTFS_BLKSZ * 8)) / 8;
    uint8_t bit = block_num % 8;
    int result;

    kprintf("free_block: Freeing block %u\n", block_num);
    if (block_num >= my_superblock.block_count) {
        kprintf("free_block: Invalid block %u, exceeds total blocks %u\n", block_num, my_superblock.block_count);
        return -EINVAL;
    }

    result = read_a_block(bitmap_block, &bitmap);
    if (result < 0) {
        kprintf("free_block: Failed to read bitmap block %u: %d\n", bitmap_block, result);
        return result;
    }

    bitmap.bytes[byte] &= ~(1 << bit);
    result = write_a_block(bitmap_block, &bitmap);
    if (result < 0) {
        kprintf("free_block: Failed to write bitmap block %u: %d\n", bitmap_block, result);
        return result;
    }
    kprintf("free_block: Freed block %u\n", block_num);
    return 0;
}

static int allocate_inode(uint16_t *inode_num) {
    struct ktfs_bitmap bitmap;
    uint32_t bitmap_block_count = my_superblock.bitmap_block_count;
    uint32_t total_inodes = my_superblock.inode_block_count * (KTFS_BLKSZ / KTFS_INOSZ);
    int result;

    kprintf("allocate_inode: Searching for free inode\n");
    for (uint32_t i = 0; i < bitmap_block_count; i++) {
        result = read_a_block(1 + i, &bitmap);
        if (result < 0) {
            kprintf("allocate_inode: Failed to read bitmap block %u: %d\n", 1 + i, result);
            return result;
        }

        for (uint32_t byte = 0; byte < KTFS_BLKSZ; byte++) {
            if (bitmap.bytes[byte] != 0xFF) {
                for (int bit = 0; bit < 8; bit++) {
                    if (!(bitmap.bytes[byte] & (1 << bit))) {
                        uint16_t inode = (i * KTFS_BLKSZ * 8) + (byte * 8) + bit;
                        if (inode >= total_inodes || inode == my_superblock.root_directory_inode) {
                            kprintf("allocate_inode: Inode %u exceeds total inodes %u or is root\n", inode, total_inodes);
                            return -ENOINODEBLKS;
                        }
                        bitmap.bytes[byte] |= (1 << bit);
                        result = write_a_block(1 + i, &bitmap);
                        if (result < 0) {
                            kprintf("allocate_inode: Failed to write bitmap block %u: %d\n", 1 + i, result);
                            return result;
                        }
                        *inode_num = inode;
                        kprintf("allocate_inode: Allocated inode %u\n", inode);
                        return 0;
                    }
                }
            }
        }
    }

    kprintf("allocate_inode: No free inodes available\n");
    return -ENOINODEBLKS;
}

static int free_inode(uint16_t inode_num) {
    struct ktfs_bitmap bitmap;
    uint32_t bitmap_block = 1 + (inode_num / (KTFS_BLKSZ * 8));
    uint32_t byte = (inode_num % (KTFS_BLKSZ * 8)) / 8;
    uint8_t bit = inode_num % 8;
    int result;

    kprintf("free_inode: Freeing inode %u\n", inode_num);
    if (inode_num >= my_superblock.inode_block_count * (KTFS_BLKSZ / KTFS_INOSZ)) {
        kprintf("free_inode: Invalid inode %u, exceeds total inodes %u\n", 
                inode_num, my_superblock.inode_block_count * (KTFS_BLKSZ / KTFS_INOSZ));
        return -EINVAL;
    }

    result = read_a_block(bitmap_block, &bitmap);
    if (result < 0) {
        kprintf("free_inode: Failed to read bitmap block %u: %d\n", bitmap_block, result);
        return result;
    }

    bitmap.bytes[byte] &= ~(1 << bit);
    result = write_a_block(bitmap_block, &bitmap);
    if (result < 0) {
        kprintf("free_inode: Failed to write bitmap block %u: %d\n", bitmap_block, result);
        return result;
    }
    kprintf("free_inode: Freed inode %u\n", inode_num);
    return 0;
}

static int update_inode(uint16_t inode_num, struct ktfs_inode *inode) {
    struct ktfs_data_block inode_block;
    int inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    uint32_t inode_block_num = where_inodes_start + (inode_num / inodes_per_block);
    uint32_t inode_offset = (inode_num % inodes_per_block) * KTFS_INOSZ;
    int result;

    kprintf("update_inode: Updating inode %u\n", inode_num);
    result = read_a_block(inode_block_num, &inode_block);
    if (result < 0) {
        kprintf("update_inode: Failed to read inode block %u: %d\n", inode_block_num, result);
        return result;
    }

    memcpy(inode_block.data + inode_offset, inode, sizeof(struct ktfs_inode));
    result = write_a_block(inode_block_num, &inode_block);
    if (result < 0) {
        kprintf("update_inode: Failed to write inode block %u: %d\n", inode_block_num, result);
        return result;
    }
    kprintf("update_inode: Updated inode %u\n", inode_num);
    return 0;
}

static int update_dir_entry(const char *name, uint16_t inode_num, int add) {
    struct ktfs_inode root_inode;
    struct ktfs_data_block inode_block, dir_block;
    struct ktfs_dir_entry *dentry;
    uint32_t num_entries;
    int result;

    kprintf("update_dir_entry: %s '%s' with inode %u\n", add ? "Adding" : "Deleting", name, inode_num);
    int inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    uint32_t root_block = where_inodes_start + (my_superblock.root_directory_inode / inodes_per_block);
    uint32_t root_offset = (my_superblock.root_directory_inode % inodes_per_block) * KTFS_INOSZ;
    kprintf("update_dir_entry: Root inode block: %u, offset: %u, root_inode_idx: %u\n", 
            root_block, root_offset, my_superblock.root_directory_inode);

    result = read_a_block(root_block, &inode_block);
    if (result < 0) {
        kprintf("update_dir_entry: Failed to read inode block %u: %d\n", root_block, result);
        return result;
    }
    
    memcpy(&root_inode, inode_block.data + root_offset, sizeof(struct ktfs_inode));
    kprintf("update_dir_entry: Root inode size: %u, block[0]: %u\n", root_inode.size, root_inode.block[0]);

    // If root directory is empty, allocate first block
    if (root_inode.size == 0) {
        uint32_t new_block_num;
        result = allocate_block(&new_block_num);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to allocate first block for root directory: %d\n", result);
            return result;
        }
        root_inode.block[0] = new_block_num;
        root_inode.size = KTFS_DENSZ;
        result = update_inode(my_superblock.root_directory_inode, &root_inode);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to update root inode with first block: %d\n", result);
            free_block(new_block_num);
            return result;
        }
        kprintf("update_dir_entry: Allocated first block %u for root directory\n", new_block_num);
        
        // Flush the cache to ensure the inode update is persisted
        result = ktfs_flush();
        if (result < 0) {
            kprintf("update_dir_entry: Failed to flush cache after inode update: %d\n", result);
            free_block(new_block_num);
            return result;
        }
        
        // Verify the inode was updated correctly
        result = read_a_block(root_block, &inode_block);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to verify inode update: %d\n", result);
            return result;
        }
        memcpy(&root_inode, inode_block.data + root_offset, sizeof(struct ktfs_inode));
        kprintf("update_dir_entry: Verified root inode size: %u, block[0]: %u\n", root_inode.size, root_inode.block[0]);
    }

    num_entries = root_inode.size / KTFS_DENSZ;
    uint32_t bytes_needed = num_entries * KTFS_DENSZ;
    uint32_t blocks_needed = (bytes_needed + KTFS_BLKSZ - 1) / KTFS_BLKSZ;

    if (add) {
        // Check if name already exists
        for (uint32_t i = 0; i < blocks_needed; i++) {
            uint32_t block_num;
            result = figure_out_block(&root_inode, i * KTFS_BLKSZ, &block_num);
            if (result < 0) {
                kprintf("update_dir_entry: Failed to get block %u: %d\n", i, result);
                continue;
            }

            result = read_a_block(block_num, &dir_block);
            if (result < 0) {
                kprintf("update_dir_entry: Failed to read data block %u: %d\n", block_num, result);
                continue;
            }

            uint32_t entries_in_block = (i == blocks_needed - 1) 
                ? (bytes_needed - i * KTFS_BLKSZ) / KTFS_DENSZ 
                : KTFS_BLKSZ / KTFS_DENSZ;
            for (uint32_t j = 0; j < entries_in_block; j++) {
                dentry = (struct ktfs_dir_entry *)(dir_block.data + j * KTFS_DENSZ);
                if (strncmp(dentry->name, name, KTFS_MAX_FILENAME_LEN) == 0) {
                    kprintf("update_dir_entry: File '%s' already exists\n", name);
                    return -EINVAL;
                }
            }
        }

        // Allocate new block if needed
        uint32_t new_block_num = 0;
        if (num_entries * KTFS_DENSZ >= blocks_needed * KTFS_BLKSZ) {
            result = allocate_block(&new_block_num);
            if (result < 0) {
                kprintf("update_dir_entry: Failed to allocate new block: %d\n", result);
                return result;
            }
            if (num_entries < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                root_inode.block[num_entries] = new_block_num;
                kprintf("update_dir_entry: Assigned new block %u to direct block %u\n", new_block_num, num_entries);
            } else if (num_entries < KTFS_NUM_DIRECT_DATA_BLOCKS + KTFS_BLKSZ / sizeof(uint32_t)) {
                struct ktfs_data_block indirect_block;
                if (root_inode.indirect == 0) {
                    uint32_t new_indirect_block = 0;  // Temporary to avoid unaligned access
                    result = allocate_block(&new_indirect_block);
                    if (result < 0) {
                        kprintf("update_dir_entry: Failed to allocate indirect block: %d\n", result);
                        free_block(new_block_num);
                        return result;
                    }
                    root_inode.indirect = new_indirect_block;  // Assign back to packed struct
                    kprintf("update_dir_entry: Allocated indirect block %u\n", root_inode.indirect);
                    
                    // Initialize the indirect block with zeros
                    memset(&indirect_block, 0, sizeof(indirect_block));
                    result = write_a_block(root_inode.indirect, &indirect_block);
                    if (result < 0) {
                        kprintf("update_dir_entry: Failed to initialize indirect block: %d\n", result);
                        free_block(new_block_num);
                        free_block(root_inode.indirect);
                        root_inode.indirect = 0;
                        return result;
                    }
                }
                result = read_a_block(root_inode.indirect, &indirect_block);
                if (result < 0) {
                    kprintf("update_dir_entry: Failed to read indirect block %u: %d\n", root_inode.indirect, result);
                    free_block(new_block_num);
                    return result;
                }
                ((uint32_t *)indirect_block.data)[num_entries - KTFS_NUM_DIRECT_DATA_BLOCKS] = new_block_num;
                result = write_a_block(root_inode.indirect, &indirect_block);
                if (result < 0) {
                    kprintf("update_dir_entry: Failed to write indirect block %u: %d\n", root_inode.indirect, result);
                    free_block(new_block_num);
                    return result;
                }
                kprintf("update_dir_entry: Assigned new block %u to indirect block index %u\n", 
                        new_block_num, num_entries - KTFS_NUM_DIRECT_DATA_BLOCKS);
            } else {
                kprintf("update_dir_entry: Directory too large for direct/indirect blocks\n");
                return -ENODATABLKS;
            }
        } else {
            result = figure_out_block(&root_inode, blocks_needed * KTFS_BLKSZ - KTFS_DENSZ, &new_block_num);
            if (result < 0) {
                kprintf("update_dir_entry: Failed to get existing block: %d\n", result);
                return result;
            }
        }

        // Add new directory entry
        result = read_a_block(new_block_num, &dir_block);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to read new block %u: %d\n", new_block_num, result);
            free_block(new_block_num);
            return result;
        }
        dentry = (struct ktfs_dir_entry *)(dir_block.data + (num_entries % (KTFS_BLKSZ / KTFS_DENSZ)) * KTFS_DENSZ);
        strncpy(dentry->name, name, KTFS_MAX_FILENAME_LEN);
        dentry->name[KTFS_MAX_FILENAME_LEN] = '\0';
        dentry->inode = inode_num;
        result = write_a_block(new_block_num, &dir_block);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to write new block %u: %d\n", new_block_num, result);
            free_block(new_block_num);
            return result;
        }
        kprintf("update_dir_entry: Added entry for '%s' with inode %u in block %u\n", name, inode_num, new_block_num);

        // Update root inode size
        root_inode.size += KTFS_DENSZ;
        result = update_inode(my_superblock.root_directory_inode, &root_inode);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to update root inode: %d\n", result);
            free_block(new_block_num);
            return result;
        }
        kprintf("update_dir_entry: Updated root inode size to %u\n", root_inode.size);
    } else {
        // Delete directory entry
        uint32_t found_block = 0;
        uint32_t found_offset = 0;
        int found = 0;

        for (uint32_t i = 0; i < blocks_needed; i++) {
            uint32_t block_num;
            result = figure_out_block(&root_inode, i * KTFS_BLKSZ, &block_num);
            if (result < 0) {
                kprintf("update_dir_entry: Failed to get block %u: %d\n", i, result);
                continue;
            }

            result = read_a_block(block_num, &dir_block);
            if (result < 0) {
                kprintf("update_dir_entry: Failed to read data block %u: %d\n", block_num, result);
                continue;
            }

            uint32_t entries_in_block = (i == blocks_needed - 1) 
                ? (bytes_needed - i * KTFS_BLKSZ) / KTFS_DENSZ 
                : KTFS_BLKSZ / KTFS_DENSZ;
            for (uint32_t j = 0; j < entries_in_block; j++) {
                dentry = (struct ktfs_dir_entry *)(dir_block.data + j * KTFS_DENSZ);
                if (strncmp(dentry->name, name, KTFS_MAX_FILENAME_LEN) == 0) {
                    found = 1;
                    found_block = block_num;
                    found_offset = j * KTFS_DENSZ;
                    kprintf("update_dir_entry: Found entry for '%s' at block %u, offset %u\n", 
                            name, found_block, found_offset);
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            kprintf("update_dir_entry: File '%s' not found\n", name);
            return -ENOENT;
        }

        // Shift entries to maintain contiguity (replace memmove with a manual loop)
        result = read_a_block(found_block, &dir_block);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to read block %u for deletion: %d\n", found_block, result);
            return result;
        }
        size_t bytes_to_shift = num_entries * KTFS_DENSZ - found_offset - KTFS_DENSZ;
        uint8_t *dest = dir_block.data + found_offset; //change
        uint8_t *src = dir_block.data + found_offset + KTFS_DENSZ; //change
        for (size_t k = 0; k < bytes_to_shift; k++) {
            dest[k] = src[k];
        }
        result = write_a_block(found_block, &dir_block);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to write block %u after deletion: %d\n", found_block, result);
            return result;
        }
        kprintf("update_dir_entry: Removed entry for '%s' from block %u\n", name, found_block);

        // Update root inode size
        root_inode.size -= KTFS_DENSZ;
        result = update_inode(my_superblock.root_directory_inode, &root_inode);
        if (result < 0) {
            kprintf("update_dir_entry: Failed to update root inode after deletion: %d\n", result);
            return result;
        }
        kprintf("update_dir_entry: Updated root inode size to %u\n", root_inode.size);
    }

    return 0;
}

// EXPORTED FUNCTION DEFINITIONS
//

int ktfs_mount(struct io *io) {
    struct ktfs_data_block superblock_block;
    int result;

    kprintf("ktfs_mount: Starting mount, io=%p\n", io);
    // save device 
    if (!io || !io->intf || !io->intf->readat) {
        kprintf("ktfs_mount: Invalid io - io=%p, intf=%p, readat=%p\n", 
                io, io ? io->intf : NULL, (io && io->intf) ? io->intf->readat : NULL);
        return -EINVAL;
    }
    my_device = io;
    kprintf("ktfs_mount: Device set to %p\n", my_device);

    kprintf("ktfs_mount: Reading superblock at offset 0, size=%d\n", KTFS_BLKSZ);
    // read superblock
    result = io->intf->readat(io, 0, &superblock_block, KTFS_BLKSZ);
    if (result < 0) {
        kprintf("ktfs_mount: Failed to read superblock: result=%d\n", result);
        return result; // couldn't read superblock
    }
    kprintf("ktfs_mount: Superblock read, result=%d bytes\n", result);

    // copy superblock into variable
    memcpy(&my_superblock, superblock_block.data, sizeof(struct ktfs_superblock));
    kprintf("Superblock: block_count=%u, bitmap_blocks=%u, inode_blocks=%u, root_inode=%u\n",
            my_superblock.block_count, my_superblock.bitmap_block_count,
            my_superblock.inode_block_count, my_superblock.root_directory_inode);

    // check superblock
    if (my_superblock.block_count == 0) {
        kprintf("ktfs_mount: Invalid block_count=%u\n", my_superblock.block_count);
        return -EINVAL;
    }
    if (my_superblock.bitmap_block_count == 0) {
        kprintf("ktfs_mount: Invalid bitmap_block_count=%u\n", my_superblock.bitmap_block_count);
        return -EINVAL;
    }
    if (my_superblock.inode_block_count == 0) {
        kprintf("ktfs_mount: Invalid inode_block_count=%u\n", my_superblock.inode_block_count);
        return -EINVAL;
    }
    kprintf("ktfs_mount: Superblock validation passed\n");

    kprintf("ktfs_mount: Creating cache with device %p\n", my_device);
    // setup cache with device
    result = create_cache(my_device, &my_cache);
    if (result < 0) {
        kprintf("create_cache failed: %d\n", result);
        return result;
    }
    if (!my_cache) {
        kprintf("create_cache succeeded but my_cache is NULL\n");
        return -ENOMEM;
    }
    kprintf("Cache created at %p\n", my_cache);

    where_inodes_start = 1 + my_superblock.bitmap_block_count; // superblock then bitmaps
    where_data_starts = where_inodes_start + my_superblock.inode_block_count; // after inodes
    kprintf("ktfs_mount: where_inodes_start=%u, where_data_starts=%u\n", 
            where_inodes_start, where_data_starts);

    // Initialize root inode
    int inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    uint32_t root_block = where_inodes_start + (my_superblock.root_directory_inode / inodes_per_block);
    uint32_t root_offset = (my_superblock.root_directory_inode % inodes_per_block) * KTFS_INOSZ;
    kprintf("ktfs_mount: Root inode block: %u, offset: %u\n", root_block, root_offset);

    struct ktfs_data_block inode_block;
    result = read_a_block(root_block, &inode_block);
    if (result < 0) {
        kprintf("ktfs_mount: Failed to read root inode block %u: %d\n", root_block, result);
        return result;
    }

    struct ktfs_inode root_inode;
    memcpy(&root_inode, inode_block.data + root_offset, sizeof(struct ktfs_inode));
    kprintf("ktfs_mount: Root inode size: %u, block[0]: %u\n", root_inode.size, root_inode.block[0]);

    // If root inode is not initialized, initialize it
    if (root_inode.size == 0 || root_inode.block[0] == 0) {
        kprintf("ktfs_mount: Initializing root inode\n");
        root_inode.size = KTFS_DENSZ;  // Size of one directory entry
        root_inode.block[0] = where_data_starts;  // First data block
        root_inode.flags = 0;
        root_inode.block[1] = 0;
        root_inode.block[2] = 0;
        root_inode.indirect = 0;
        root_inode.dindirect[0] = 0;
        root_inode.dindirect[1] = 0;

        // Write back the initialized root inode
        memcpy(inode_block.data + root_offset, &root_inode, sizeof(struct ktfs_inode));
        result = write_a_block(root_block, &inode_block);
        if (result < 0) {
            kprintf("ktfs_mount: Failed to write initialized root inode: %d\n", result);
            return result;
        }
        kprintf("ktfs_mount: Root inode initialized with size=%u, block[0]=%u\n", 
                root_inode.size, root_inode.block[0]);
    }

    kprintf("ktfs_mount: Initializing file array\n");
    memset(my_files, 0, sizeof(my_files));
    for (int i = 0; i < MAX_FILES_OPEN; i++) {
        my_files[i].flags = KTFS_FILE_FREE;
        kprintf("ktfs_mount: File slot %d set to FREE\n", i);
    }

    kprintf("ktfs_mount: Mount completed successfully\n");
    return 0;
}

int ktfs_open(const char *name, struct io **ioptr) {
    uint16_t inode_num;
    struct ktfs_inode inode;
    int result;

    if (!name || !ioptr) {
        kprintf("ktfs_open: Invalid arguments (name=%p, ioptr=%p)\n", name, ioptr);
        return -EINVAL;
    }
    if (!my_device || !my_cache) {
        kprintf("ktfs_open: Filesystem not mounted (my_device=%p, my_cache=%p)\n", my_device, my_cache);
        return -EINVAL;
    }
    kprintf("ktfs_open: Opening file '%s'\n", name);

    // check length
    size_t name_len = strlen(name);
    if (name_len > KTFS_MAX_FILENAME_LEN) {
        kprintf("ktfs_open: Filename '%s' too long (%u > %u)\n", name, name_len, KTFS_MAX_FILENAME_LEN);
        return -EINVAL;
    }

    // find inode number
    result = find_file_inode(name, &inode_num);
    if (result < 0) {
        kprintf("ktfs_open: find_file_inode failed for '%s': %d\n", name, result);
        return result;
    }
    kprintf("ktfs_open: Found inode number %u for '%s'\n", inode_num, name);

    // Find a free slot in my_files
    int free_spot = -1;
    for (int i = 0; i < MAX_FILES_OPEN; i++) {
        if (my_files[i].flags == KTFS_FILE_FREE) {
            free_spot = i;
            break;
        }
    }
    if (free_spot == -1) {
        kprintf("ktfs_open: No free file slots available\n");
        return -EMFILE;
    }

    // calcualte offset
    int inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    uint32_t inode_block = where_inodes_start + (inode_num / inodes_per_block);
    uint32_t inode_offset = (inode_num % inodes_per_block) * KTFS_INOSZ;

    // read inode block
    struct ktfs_data_block inode_block_data;
    result = read_a_block(inode_block, &inode_block_data);
    if (result < 0) {
        kprintf("ktfs_open: read_a_block failed for inode block %u: %d\n", inode_block, result);
        return result;
    }

    // get inode
    memcpy(&inode, inode_block_data.data + inode_offset, sizeof(struct ktfs_inode));

    // setup file struct
    struct ktfs_file *file = &my_files[free_spot];
    file->flags = KTFS_FILE_IN_USE;
    file->size = inode.size;
    file->inode = inode;
    file->inode_num = inode_num;

    strncpy(file->dentry.name, name, KTFS_MAX_FILENAME_LEN);
    file->dentry.name[KTFS_MAX_FILENAME_LEN] = '\0'; // Ensure null termination
    file->dentry.inode = inode_num;

    // allocate + ioiotf
    struct iointf *interface = kmalloc(sizeof(struct iointf));
    if (!interface) {
        kprintf("ktfs_open: kmalloc failed for iointf\n");
        file->flags = KTFS_FILE_FREE; // Cleanup on failure
        return -ENOMEM;
    }
    interface->close = ktfs_close;
    interface->cntl = ktfs_cntl;
    interface->read = NULL;     
    interface->write = NULL;    
    interface->readat = ktfs_readat;
    interface->writeat = ktfs_writeat;

    ioinit1(&file->io, interface);
    ioaddref(&file->io); // increment reference count
    *ioptr = create_seekable_io(&file->io);
    if (*ioptr == NULL) {
        kprintf("ktfs_open: create_seekable_io failed\n");
        file->flags = KTFS_FILE_FREE;
        kfree((struct iointf *)file->io.intf);
        return -ENOMEM;
    }
    file->seekable_io = *ioptr;  // Store the seekable IO pointer
    how_many_files_open++;

    kprintf("ktfs_open: Successfully opened '%s' at slot %d, ioptr=%p\n", name, free_spot, *ioptr);
    return 0;
}

void ktfs_close(struct io *io) {
    // search from io
    struct ktfs_file *file = find_file_from_io(io);
    if (!file) {
        kprintf("ktfs_close: Invalid file for io=%p\n", io);
        return;
    }
    if (file->flags != KTFS_FILE_IN_USE) {
        kprintf("ktfs_close: File not in use for io=%p\n", io);
        return;
    }

    // mark file as free and update count
    file->flags = KTFS_FILE_FREE;
    how_many_files_open--;
    kfree((struct iointf *)file->io.intf);
    if (file->seekable_io) {
        kfree(file->seekable_io);
        file->seekable_io = NULL;
    }
    kprintf("ktfs_close: Closed file with io=%p\n", io);
}

long ktfs_readat(struct io *io, unsigned long long pos, void *buf, long len) {
    kprintf("ktfs_readat: ENTER - io=%p, pos=%llu, buf=%p, len=%ld\n", io, pos, buf, len);
    
    // get file
    struct ktfs_file *file = find_file_from_io(io);
    if (!file) {
        kprintf("ktfs_readat: Invalid file for io=%p\n", io);
        return -EBADFD;
    }
    kprintf("ktfs_readat: Found file at %p\n", file);
    
    if (file->flags != KTFS_FILE_IN_USE) {
        kprintf("ktfs_readat: File not in use for io=%p\n", io);
        return -EBADFD;
    }
    if (!buf || len < 0) {
        kprintf("ktfs_readat: Invalid buffer or length (buf=%p, len=%ld)\n", buf, len);
        return -EINVAL;
    }

    if (pos >= file->size) {
        kprintf("ktfs_readat: Position %llu exceeds file size %u\n", pos, file->size);
        return 0;
    }
    
    long total_bytes_read = 0;
    char *destination = (char *)buf;
    long bytes_left = len;
    uint64_t current_pos = pos;

    while (bytes_left > 0 && current_pos < file->size) {
        uint32_t block_number = current_pos / KTFS_BLKSZ;
        uint32_t block_offset = current_pos % KTFS_BLKSZ;
        uint32_t bytes_to_read = (bytes_left < (KTFS_BLKSZ - block_offset)) ? bytes_left : (KTFS_BLKSZ - block_offset);
        bytes_to_read = (bytes_to_read < (file->size - current_pos)) ? bytes_to_read : (file->size - current_pos);

        kprintf("ktfs_readat: Reading block %u, offset %u, bytes %u\n", 
                block_number, block_offset, bytes_to_read);

        // Get the actual block number from the inode
        uint32_t data_block_num;
        int result = figure_out_block(&file->inode, current_pos, &data_block_num);
        if (result < 0) {
            kprintf("ktfs_readat: Failed to get block number for pos %llu: %d\n", current_pos, result);
            return result;
        }

        // Calculate the actual block position in the filesystem
        uint32_t block_pos = (where_data_starts + data_block_num) * KTFS_BLKSZ;
        kprintf("ktfs_readat: Block position calculation:\n");
        kprintf("  - Logical block number: %u\n", block_number);
        kprintf("  - Data block number from inode: %u\n", data_block_num);
        kprintf("  - Data section start: %u\n", where_data_starts);
        kprintf("  - Block size: %u\n", KTFS_BLKSZ);
        kprintf("  - Final block position: %u\n", block_pos);
        kprintf("  - Block offset within block: %u\n", block_offset);
        kprintf("  - Bytes to read: %u\n", bytes_to_read);

        void *block_data;
        result = cache_get_block(my_cache, block_pos, &block_data);
        if (result < 0) {
            kprintf("ktfs_readat: Failed to get block %u: %d\n", data_block_num, result);
            return result;
        }

        kprintf("ktfs_readat: Raw block data at position %u:\n", block_pos);
        for (int i = 0; i < 64; i++) {
            kprintf("  [%d] = 0x%02x\n", i, ((unsigned char *)block_data)[i]);
        }

        kprintf("ktfs_readat: Memory copy details:\n");
        kprintf("  - Source address: 0x%lx + %d = 0x%lx\n", (uintptr_t)block_data, block_offset, (uintptr_t)block_data + block_offset);
        kprintf("  - Destination address: 0x%lx\n", (uintptr_t)buf);
        kprintf("  - Copy size: %d bytes\n", bytes_to_read);

        // Print the actual source data at the correct offset
        kprintf("ktfs_readat: Source data at offset %d:\n", block_offset);
        for (int i = 0; i < bytes_to_read && i < 32; i++) {
            kprintf("  [%d] = 0x%02x\n", i, ((unsigned char *)block_data)[block_offset + i]);
        }

        memcpy(buf, block_data + block_offset, bytes_to_read);

        // Print the destination data
        kprintf("ktfs_readat: Destination data:\n");
        for (int i = 0; i < bytes_to_read && i < 32; i++) {
            kprintf("  [%d] = 0x%02x\n", i, ((unsigned char *)buf)[i]);
        }

        kprintf("ktfs_readat: Copied %d bytes from block 0x%lx to buffer 0x%lx\n", bytes_to_read, (uintptr_t)block_data, (uintptr_t)buf);

        total_bytes_read += bytes_to_read;
        destination += bytes_to_read;
        bytes_left -= bytes_to_read;
        current_pos += bytes_to_read;
    }

    kprintf("ktfs_readat: Successfully read %ld bytes\n", total_bytes_read);
    return total_bytes_read;
}

int ktfs_cntl(struct io *io, int cmd, void *arg) {
    kprintf("ktfs_cntl: ENTER - io=%p, cmd=%d, arg=%p\n", io, cmd, arg);
    
    // get file
    struct ktfs_file *file = find_file_from_io(io);
    if (!file || file->flags != KTFS_FILE_IN_USE) {
        kprintf("ktfs_cntl: Invalid file or not in use for io=%p\n", io);
        return -EBADFD;
    }
    kprintf("ktfs_cntl: Found valid file at %p\n", file);

    // check which command
    if (cmd == IOCTL_GETEND) {  
        kprintf("ktfs_cntl: Processing IOCTL_GETEND\n");
        if (!arg) {
            kprintf("ktfs_cntl: Invalid arg for IOCTL_GETEND\n");
            return -EINVAL;
        }
        *(unsigned long long *)arg = file->size;
        kprintf("ktfs_cntl: File size = %u bytes\n", file->size);
        return 0;
    }
    else if (cmd == IOCTL_GETBLKSZ) {
        kprintf("ktfs_cntl: Processing IOCTL_GETBLKSZ\n");
        if (!arg) {
            kprintf("ktfs_cntl: Invalid arg for IOCTL_GETBLKSZ\n");
            return -EINVAL;
        }
        *(int *)arg = KTFS_BLKSZ;
        kprintf("ktfs_cntl: Block size = %d bytes\n", KTFS_BLKSZ);
        return 1;
    }
    else if (cmd == IOCTL_SETEND) {
        kprintf("ktfs_cntl: Processing IOCTL_SETEND\n");
        if (!arg) {
            kprintf("ktfs_cntl: Invalid arg for IOCTL_SETEND\n");
            return -EINVAL;
        }
        unsigned long long new_size = *(const unsigned long long *)arg;
        kprintf("ktfs_cntl: Setting file size to %llu\n", new_size);
        if (new_size == file->size) {
            kprintf("ktfs_cntl: File size unchanged\n");
            return 0;
        }
        file->size = new_size;
        file->inode.size = new_size;
        int result = update_inode(file->inode_num, &file->inode);
        if (result < 0) {
            kprintf("ktfs_cntl: Failed to update inode %u: %d\n", file->inode_num, result);
            return result;
        }
        kprintf("ktfs_cntl: File size set to %llu\n", new_size);
        return 0;
    }
    else if (cmd == IOCTL_SETPOS) {
        kprintf("ktfs_cntl: Processing IOCTL_SETPOS\n");
        if (!arg) {
            kprintf("ktfs_cntl: Invalid arg for IOCTL_SETPOS\n");
            return -EINVAL;
        }
        unsigned long long new_pos = *(const unsigned long long *)arg;
        kprintf("ktfs_cntl: Setting position to %llu\n", new_pos);
        
        // Position is valid, update the seekable_io's position
        if (file->seekable_io) {
            kprintf("ktfs_cntl: Found seekable_io at %p\n", file->seekable_io);
            int result = ioctl(file->seekable_io, IOCTL_SETPOS, &new_pos);
            kprintf("ktfs_cntl: ioctl result for seekable_io: %d\n", result);
            if (result < 0) {
                kprintf("ktfs_cntl: Failed to set position in seekable_io: %d\n", result);
                return result;
            }
            kprintf("ktfs_cntl: Position set to %llu\n", new_pos);
            return 0;
        }
        kprintf("ktfs_cntl: No seekable_io available\n");
        return -EINVAL;
    }
    else {
        kprintf("ktfs_cntl: Unsupported command %d\n", cmd);
        return -ENOTSUP;
    }
}

int ktfs_flush(void) {
    if (!my_cache) {
        kprintf("ktfs_flush: my_cache is NULL\n");
        return -EINVAL;
    }
    int result = cache_flush(my_cache);
    if (result < 0) {
        kprintf("ktfs_flush: cache_flush failed: %d\n", result);
        return result;
    }
    kprintf("ktfs_flush: Cache flushed successfully\n");
    return result;
}

long ktfs_writeat(struct io *io, unsigned long long pos, const void *buf, long len) {
    struct ktfs_file *file = find_file_from_io(io);
    if (!file || file->flags != KTFS_FILE_IN_USE) {
        kprintf("ktfs_writeat: Invalid file or not in use for io=%p\n", io);
        return -EBADFD;
    }
    if (!buf || len < 0) {
        kprintf("ktfs_writeat: Invalid buffer or length (buf=%p, len=%ld)\n", buf, len);
        return -EINVAL;
    }

    long total_bytes_written = 0;
    const char *source = (const char *)buf;
    long bytes_left = len;
    uint64_t current_pos = pos;
    kprintf("ktfs_writeat: Writing %ld bytes to pos %llu\n", len, pos);

    while (bytes_left > 0) {
        uint32_t block_num;
        int result = figure_out_block(&file->inode, current_pos, &block_num);
        if (result < 0) {
            uint32_t block_idx = current_pos / KTFS_BLKSZ;
            if (block_idx >= KTFS_NUM_DIRECT_DATA_BLOCKS + KTFS_BLKSZ / sizeof(uint32_t)) {
                kprintf("ktfs_writeat: File too large for direct/indirect blocks at block index %u\n", block_idx);
                return -ENODATABLKS;
            }
            result = allocate_block(&block_num);
            if (result < 0) {
                kprintf("ktfs_writeat: Failed to allocate block %u: %d\n", block_idx, result);
                return result;
            }
            if (block_idx < KTFS_NUM_DIRECT_DATA_BLOCKS) {
                file->inode.block[block_idx] = block_num;
                kprintf("ktfs_writeat: Assigned block %u to direct block %u\n", block_num, block_idx);
            } else {
                struct ktfs_data_block indirect_block;
                if (file->inode.indirect == 0) {
                    uint32_t new_indirect_block = 0;  // Temporary to avoid unaligned access
                    result = allocate_block(&new_indirect_block);
                    if (result < 0) {
                        kprintf("ktfs_writeat: Failed to allocate indirect block: %d\n", result);
                        free_block(block_num);
                        return result;
                    }
                    file->inode.indirect = new_indirect_block;  // Assign back to packed struct
                    kprintf("ktfs_writeat: Allocated indirect block %u\n", file->inode.indirect);
                    
                    // Initialize the indirect block with zeros
                    memset(&indirect_block, 0, sizeof(indirect_block));
                    result = write_a_block(file->inode.indirect, &indirect_block);
                    if (result < 0) {
                        kprintf("ktfs_writeat: Failed to initialize indirect block: %d\n", result);
                        free_block(block_num);
                        free_block(file->inode.indirect);
                        file->inode.indirect = 0;
                        return result;
                    }
                }
                result = read_a_block(file->inode.indirect, &indirect_block);
                if (result < 0) {
                    kprintf("ktfs_writeat: Failed to read indirect block %u: %d\n", file->inode.indirect, result);
                    free_block(block_num);
                    return result;
                }
                ((uint32_t *)indirect_block.data)[block_idx - KTFS_NUM_DIRECT_DATA_BLOCKS] = block_num;
                result = write_a_block(file->inode.indirect, &indirect_block);
                if (result < 0) {
                    kprintf("ktfs_writeat: Failed to write indirect block %u: %d\n", file->inode.indirect, result);
                    free_block(block_num);
                    return result;
                }
                kprintf("ktfs_writeat: Assigned block %u to indirect block index %u\n", 
                        block_num, block_idx - KTFS_NUM_DIRECT_DATA_BLOCKS);
            }
            
            // Update inode immediately after block allocation
            result = update_inode(file->inode_num, &file->inode);
            if (result < 0) {
                kprintf("ktfs_writeat: Failed to update inode %u: %d\n", file->inode_num, result);
                free_block(block_num);
                return result;
            }
            
            // Flush cache to ensure changes are written to disk
            result = ktfs_flush();
            if (result < 0) {
                kprintf("ktfs_writeat: Failed to flush cache: %d\n", result);
                free_block(block_num);
                return result;
            }
            
            // Try to find the block again after updating inode
            result = figure_out_block(&file->inode, current_pos, &block_num);
            if (result < 0) {
                kprintf("ktfs_writeat: Failed to find block after allocation: %d\n", result);
                free_block(block_num);
                return result;
            }
        }

        struct ktfs_data_block data_block;
        result = read_a_block(block_num, &data_block);
        if (result < 0) {
            kprintf("ktfs_writeat: Failed to read block %u: %d\n", block_num, result);
            return result;
        }

        int block_offset = current_pos % KTFS_BLKSZ;
        int bytes_to_write = KTFS_BLKSZ - block_offset;
        if (bytes_to_write > bytes_left) {
            bytes_to_write = bytes_left;
        }

        memcpy(data_block.data + block_offset, source + total_bytes_written, bytes_to_write);
        result = write_a_block(block_num, &data_block);
        if (result < 0) {
            kprintf("ktfs_writeat: Failed to write block %u: %d\n", block_num, result);
            return result;
        }
        kprintf("ktfs_writeat: Wrote %d bytes to block %u, offset %d\n", bytes_to_write, block_num, block_offset);

        total_bytes_written += bytes_to_write;
        bytes_left -= bytes_to_write;
        current_pos += bytes_to_write;
    }

    return total_bytes_written;
}

int ktfs_create(const char *name) {
    kprintf("ktfs_create: Creating file '%s'\n", name);
    if (!name || !my_device || !my_cache) {
        kprintf("ktfs_create: Invalid arguments or unmounted filesystem (name=%p, my_device=%p, my_cache=%p)\n", 
                name, my_device, my_cache);
        return -EINVAL;
    }

    size_t name_len = strlen(name);
    if (name_len > KTFS_MAX_FILENAME_LEN) {
        kprintf("ktfs_create: Filename '%s' too long (%u > %u)\n", name, name_len, KTFS_MAX_FILENAME_LEN);
        return -EINVAL;
    }

    uint16_t inode_num;
    int result = allocate_inode(&inode_num);
    if (result < 0) {
        kprintf("ktfs_create: Failed to allocate inode: %d\n", result);
        return result;
    }

    struct ktfs_inode inode = {0};
    inode.size = 0;
    result = update_inode(inode_num, &inode);
    if (result < 0) {
        kprintf("ktfs_create: Failed to update inode %u: %d\n", inode_num, result);
        free_inode(inode_num);
        return result;
    }
    kprintf("ktfs_create: Initialized inode %u with size 0\n", inode_num);

    result = update_dir_entry(name, inode_num, 1);
    if (result < 0) {
        kprintf("ktfs_create: Failed to add directory entry for '%s': %d\n", name, result);
        free_inode(inode_num);
        return result;
    }

    kprintf("ktfs_create: Successfully created '%s' with inode %u\n", name, inode_num);
    return 0;
}

int ktfs_delete(const char *name) {
    kprintf("ktfs_delete: Deleting file '%s'\n", name);
    if (!name || !my_device || !my_cache) {
        kprintf("ktfs_delete: Invalid arguments or unmounted filesystem (name=%p, my_device=%p, my_cache=%p)\n", 
                name, my_device, my_cache);
        return -EINVAL;
    }

    uint16_t inode_num;
    int result = find_file_inode(name, &inode_num);
    if (result < 0) {
        kprintf("ktfs_delete: File '%s' not found: %d\n", name, result);
        return result;
    }

    // Check if file is open
    for (int i = 0; i < MAX_FILES_OPEN; i++) {
        if (my_files[i].flags == KTFS_FILE_IN_USE && my_files[i].inode_num == inode_num) {
            kprintf("ktfs_delete: File '%s' is currently open\n", name);
            return -EBUSY;
        }
    }

    int inodes_per_block = KTFS_BLKSZ / KTFS_INOSZ;
    uint32_t inode_block = where_inodes_start + (inode_num / inodes_per_block);
    uint32_t inode_offset = (inode_num % inodes_per_block) * KTFS_INOSZ;

    struct ktfs_data_block inode_block_data;
    result = read_a_block(inode_block, &inode_block_data);
    if (result < 0) {
        kprintf("ktfs_delete: Failed to read inode block %u: %d\n", inode_block, result);
        return result;
    }

    struct ktfs_inode inode;
    memcpy(&inode, inode_block_data.data + inode_offset, sizeof(struct ktfs_inode));
    kprintf("ktfs_delete: Loaded inode %u with size %u\n", inode_num, inode.size);

    // Free data blocks
    uint32_t num_blocks = (inode.size + KTFS_BLKSZ - 1) / KTFS_BLKSZ;
    for (uint32_t i = 0; i < num_blocks; i++) {
        uint32_t block_num;
        result = figure_out_block(&inode, i * KTFS_BLKSZ, &block_num);
        if (result < 0) {
            kprintf("ktfs_delete: Failed to get block %u for inode %u: %d\n", i, inode_num, result);
            continue;
        }
        result = free_block(block_num);
        if (result < 0) {
            kprintf("ktfs_delete: Failed to free block %u: %d\n", block_num, result);
            return result;
        }
    }

    // Free indirect block if used
    if (inode.indirect) {
        result = free_block(inode.indirect);
        if (result < 0) {
            kprintf("ktfs_delete: Failed to free indirect block %u: %d\n", inode.indirect, result);
            return result;
        }
        kprintf("ktfs_delete: Freed indirect block %u\n", inode.indirect);
    }

    // Free inode
    result = free_inode(inode_num);
    if (result < 0) {
        kprintf("ktfs_delete: Failed to free inode %u: %d\n", inode_num, result);
        return result;
    }

    // Remove directory entry
    result = update_dir_entry(name, inode_num, 0);
    if (result < 0) {
        kprintf("ktfs_delete: Failed to remove directory entry for '%s': %d\n", name, result);
        return result;
    }

    kprintf("ktfs_delete: Successfully deleted '%s' with inode %u\n", name, inode_num);
    return 0;
}