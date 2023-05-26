/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include <errno.h>

#define SIZE 0xFFFFFFFFFFFFFFF8 

sf_block *checkQuickList(size_t adjSize);
sf_block *checkFreeList(size_t adjSize, size_t ogSize);
sf_block *getMemory();
sf_block *setFreeBlock(sf_block *pointer, size_t size, int option);
sf_block *setAllocBlock(sf_block *pointer, size_t size);
sf_block *splitFreeBlock(sf_block *block, size_t adjSize);
int getIndex(size_t size);
void addToFreeList(sf_block *block);
void removeFromFreeList(sf_block *block);
sf_block *coalesce(sf_block *block);

sf_block *prologue = NULL;
sf_block *epilogue = NULL;
int freelist_intialized = -1;

void *sf_malloc(size_t size) {
    /* NOTES
    - word as 2 bytes (16 bits)
    - memory row as 4 words (64 bits)
    - page of memory to be 4096 bytes (4 KB)

    Main Free Lists:
    - Free blocks will be stored in a fixed array of NUM_FREE_LISTS free lists
    - first free list (at index 0) holds blocks of the minimum size M (where M = 32)
    - second list (at index 1) holds blocks of size (M, 2M]
    - third list (at index 2) holds blocks of size (2M, 4M]
    - continues up to the interval (128M, 256M]
    
    Quick Lists:
    - organized as singly linked lists accessed in LIFO fashion
    - The first quick list holds blocks of the minimum size (32 bytes)
    - second quick list holds blocks of the minimum size plus the alignment size (32+8 = 40 bytes)
    - third quick list holds blocks of size 32+8+8 = 48 byte
    - When a small block is freed, it is inserted at the front of the corresponding quick list
    - if insertion of a block would exceed the capacity of the quick list, then the list is "flushed" and the existing blocks in the quick list are removed 
        from the quick list and added to the main free list, after coalescing, if possible

    Allocating Policies:
    - segregated fits policy
        Quick List:
    - 1) quick list containing blocks of the appropriate size is first checked to try to quickly obtain a block of exactly the right size.
    - 2) If there is no quick list of that size (quick lists are only maintained for a fixed set of the smallest block sizes), or if there is a quick list 
        but it is empty, then the request will be satisfied from the main free lists

        Main List:
    - 1) the smallest size class that is sufficiently large to satisfy the request is determined
    - 2) free lists are then searched, starting from the list for the determined size class and continuing in increasing order of size, until a nonempty
         list is found.
    - 3) first-fit policy

        No Match:
    - 1) sf_mem_grow should be called to extend the heap by an additional page of memory
    - 2) coalescing this page with any free block that immediately precedes it, you should attempt to use the resulting block of memory to satisfy the 
        allocation request; splitting it if it is too large and no splinter would result
    - 3) Call sf_mem_grow again if not large enough or the return value from sf_mem_grow indicates that there is no more memory

    Splitting Blocks:
    - allocater must splot blocks to prevent internal fragmentation
    - pointers returned by the allocator in response to allocation requests are required to be aligned to 8-byte boundaries (addresses multiples of 2^3)
    - guaranteed that min block size will be 32 bytes
    - If splitting block means that the splinter will be < 32 bytes than it should not split and just allocate 
    */

   /* Suppose size == 25 bytes
        Additional 8 bytes to store block header (refer back to file for header format)
        25 + 8 == 33 bytes -> alignment requirements of 8 bytes ->  40 bytes
        40 bytes = 33 bytes + 7 bytes of padding

        When block is free:
        - Includes header but not payload
        - Need to store footer, next, and previous links = 24 bytes
        - Since no payload when free there is extra 25 bytes + 7 padding = 32 bytes free bytes
        - Since 32 < 40 bytes free is fine
        - Block cannot be smaller than 32 bytes
   */



    // Determine if size is 0
    if (size == 0) 
        return NULL;
    
    // Non-zero:
    size_t total_size = size + 8;
    if(total_size % 8 != 0) {
        total_size = total_size + 8 - (total_size % 8);
    }


    if(total_size < 32) {
        total_size = 32;
        size = 32;
    }
    
    // First check quicklist 
    sf_block *block = checkQuickList(total_size);
    if(block != NULL) {
        return block;
    }

    // Then check main free list
    void *alloc_block = checkFreeList(total_size, size);

    if(alloc_block == NULL) {
        sf_errno = ENOMEM;
        return NULL;
    }

    return (char *)alloc_block + 8;
}

sf_block *checkQuickList(size_t adjSize) {
    /*  Quick List:
    - sf_quick_lists[NUM_QUICK_LISTS] os size [20] contains pointers to the quick lists in LIFO fashion
    - sf_quick_lists[0] contains quick lists with size of MIN_BLOCK_SIZE
    - size of quicklist = MIN_BLOCK_SIZE + (index * 8)
    - contains length field with a max of 5 blocks in each quick list 
    - inserting into a quick list at capcity causes it to be flushed and the existing blocks in the quick list are removed from the quick list and added 
        to the main free list, after coalescing, if possible.
    */   

    // Finds the first free space in a quicklist and returns a pointer
    // else returns NULL if no free space found

    if(adjSize < 32 + ((NUM_QUICK_LISTS - 1)* 8))
        return NULL;

    // Find appropriate index for size in quick list
    int index = 0;
    while(index < NUM_QUICK_LISTS) {
        int quick_size = 32 + (index * 8);

        if(adjSize <= quick_size) {
            // Search for free blocks in this index
            if(sf_quick_lists[index].length > 0) {
                sf_quick_lists[index].length -= 1;

                sf_block *block = sf_quick_lists[index].first;
                block->header &= ~IN_QUICK_LIST;

                // Adjust the linked list
                sf_block *new_first = block->body.links.next;
                sf_quick_lists[index].first = new_first;
                new_first->body.links.prev = NULL;

                // Set block links to NULL
                block->body.links.next = NULL;
                block->body.links.prev = NULL;

                removeFromFreeList(block);
                return setAllocBlock(block, block->header & SIZE);
            }

            break;
        }

        index++;
    }

    return NULL;
}

sf_block *checkFreeList(size_t adjSize, size_t ogSize) {
    // - first free list (at index 0) holds blocks of the minimum size M (where M = 32)
    // - second list (at index 1) holds blocks of size (M, 2M]
    // - third list (at index 2) holds blocks of size (2M, 4M]
    // - continues up to the interval (128M, 256M]

    // Find min index with enough space
    int min = 0;
    int max = 32;
    int index = 0;
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        if (adjSize > min && adjSize <= max) {
            index = i;
            break;
        }

        /* Update min_size and max_size for the next free list */
        min = max;
            max = max * 2;
    }

    // Intialize sentinal nodes
    if(freelist_intialized == -1) {
        for (int i = 0; i < NUM_FREE_LISTS; i++) {
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }
        freelist_intialized = 0;
    }

    // Scan free list for free block
    while (index < NUM_FREE_LISTS) {
        sf_block *sentinel = &sf_free_list_heads[index];
        sf_block *current = sentinel->body.links.next;

        while(current != sentinel) {
            if((current->header & SIZE) >= adjSize) {
                return splitFreeBlock(current, adjSize);
            }

            current = current->body.links.next;
        }
        
        index++;
    }

    sf_block *block = getMemory();
    if(block == NULL) {
        return NULL;
    }


    return checkFreeList(adjSize, ogSize);  
}

sf_block *getMemory() {
    void *start = sf_mem_grow();
    // void *beginning = sf_mem_start();
    void *end = sf_mem_end();

    // No more memory 
    if(start == NULL) {
        return NULL;
    }   

    //Set new epilogue
    if(epilogue != NULL) {
        // Old epilogue becomes header of new block
        char *new_epilogue = (char *)end - 8;

        sf_header *new_page = (sf_header *) start;
        *new_page = (PAGE_SZ - 8) | PREV_BLOCK_ALLOCATED;

        addToFreeList((sf_block *) new_page);

        epilogue->header = 8 | THIS_BLOCK_ALLOCATED; // old ep
        sf_block *old_ep = epilogue;
        // Set new epilogue
        epilogue = (sf_block *)new_epilogue;
        epilogue->header = 0;
        epilogue->header |= THIS_BLOCK_ALLOCATED;

        // sf_show_heap();
        return coalesce(old_ep);
    }

    // Calculate the number of padding bytes needed to align the block header to an 8-byte boundary
    size_t padding = 0;
    size_t offset = ((size_t)start) % 8;
    if (offset != 0) {
        padding += 8;
        padding = 8 - offset;
    }

    // Header is 8 bytes aligned
    char *header = (char *)start + padding;
    prologue = (sf_block *) header;
    prologue = setAllocBlock(prologue, 32);

    char *free_start = header + (prologue->header & SIZE);
    char *epilogue_start = (char *)end - 8;

    // Set free block
    size_t free_size = (size_t)(epilogue_start - free_start);
    sf_block *free_block = setFreeBlock((sf_block *)free_start, free_size, 1);
    addToFreeList(free_block);

    // Set epilogue
    epilogue = (sf_block *)epilogue_start;
    epilogue->header = 0;
    epilogue->header |= THIS_BLOCK_ALLOCATED;

    // sf_show_heap();
    return free_block;
    
}


sf_block *setFreeBlock(sf_block *pointer, size_t size, int option) {   
    // How to get the top block and check if it's allocated 
    // Set the bottom block prev alloc to 0 (if there is one)
    sf_block *free = pointer;
    free->header = size;
    free->header &= ~THIS_BLOCK_ALLOCATED;
    free->header &= ~IN_QUICK_LIST;
    char *footer_start = (char *)free + size - 8;
    sf_footer *footer = (sf_footer *)footer_start;

    // Just free
    if(option == 0) {
        *footer = free->header;
    }

    // Set prev alloc
    else if(option == 1) {
        free->header |= PREV_BLOCK_ALLOCATED;
    }
    
    // Set in quicklist
    else if(option == 2) {
        free->header |= IN_QUICK_LIST;
    }
    
    *footer = free->header;
    return free;
}

sf_block *setAllocBlock(sf_block *block, size_t size) {
    int prev = block->header & PREV_BLOCK_ALLOCATED;

    sf_block *alloc = block;
    alloc->header = size;

    alloc->header |= THIS_BLOCK_ALLOCATED;
    alloc->header &= ~IN_QUICK_LIST;

    if(prev == 0) {
        alloc->header &= ~PREV_BLOCK_ALLOCATED;
    }
    else {
        alloc->header |= PREV_BLOCK_ALLOCATED;
    }


    return alloc;
}   

sf_block *splitFreeBlock(sf_block *block, size_t adjSize) {
    // Remove block from main free list
    removeFromFreeList(block);

    // Splitting would leave splinter(extra space < 32 bytes)
    if ((block->header & SIZE) - adjSize < 32) {
        sf_block *alloc = setAllocBlock(block, adjSize);

        // Set next block(epilogue) prev bit to 1
        char *next_ptr = (char *)alloc + (alloc->header & SIZE);
        sf_block *next_block = (sf_block *)next_ptr;
        next_block->header |= PREV_BLOCK_ALLOCATED;

        return block;
    }

    // Otherwise, split the free block
    char *new_free_pointer = (char *)block + adjSize;
    sf_block *new_free = setFreeBlock((sf_block *)new_free_pointer, (block->header & SIZE) - adjSize, 1);
    coalesce(new_free);
    // addToFreeList(new_free);

    // Create block
    sf_block *alloc = setAllocBlock(block, adjSize);

    // sf_show_heap();
    return alloc;
        
}

int getIndex(size_t size) {
    if (size <= 32) {
        return 0;
    }

    if (size > 256 * 32) {
        return 9;
    }

    int index = 1;
    int min = 32;
    int max = 2 * min;
    while (index < NUM_FREE_LISTS - 1) {
        if(size >= min && size < max) {
            break;
        }
        min = max;
        max = 2 * min;
        index++;
    }

    return index;
}

void addToFreeList(sf_block *block) {
    int index = getIndex(block->header & SIZE);

    block->body.links.next = &sf_free_list_heads[index];
    block->body.links.prev = sf_free_list_heads[index].body.links.prev;
    sf_free_list_heads[index].body.links.prev->body.links.next = block;
    sf_free_list_heads[index].body.links.prev = block;

}

void removeFromFreeList(sf_block *block) {
    block->body.links.prev->body.links.next = block->body.links.next;
    block->body.links.next->body.links.prev = block->body.links.prev;
    block->body.links.next = NULL;
    block->body.links.prev = NULL;
}

sf_block *coalesce(sf_block *block) {
    // Block is a  block that is free/ allocated block just freed
    // Removes the free adjacent blocks from the free list and adds the nwe coalesced block
    // Sets block to free and returns a coalesced free block

    char *bottom_header = (char *)block + (block->header & SIZE);
    sf_block *bottom = (sf_block *)bottom_header;

    // Top and bottom block are free
    if((block->header & PREV_BLOCK_ALLOCATED) == 0 && (bottom->header & THIS_BLOCK_ALLOCATED) == 0) {
        // Get top 
        char *footer = (char *)block - 8;
        sf_footer *actual_footer = (sf_footer *) footer;

        size_t top_size = *actual_footer & SIZE;
        size_t current_size = block->header & SIZE;
        size_t bottom_size = bottom->header & SIZE;

        // Remove from free list
        char *top = (char *)block - top_size;
        removeFromFreeList((sf_block *)top);
        removeFromFreeList(bottom);
        

        // Set new coalesce block
        sf_block *free;
        if((*actual_footer & PREV_BLOCK_ALLOCATED) == 0) {
            free = setFreeBlock((sf_block *)top, top_size + current_size + bottom_size, 0);
        }
        else {
            free = setFreeBlock((sf_block *)top, top_size + current_size + bottom_size, 1);
        }
         
        addToFreeList(free);
        // sf_show_heap();
        return free;
    }

    // Top block is free and bottom is allocated
    else if((block->header & PREV_BLOCK_ALLOCATED) == 0 && (bottom->header & THIS_BLOCK_ALLOCATED)) {
        // Get top 
        char *footer = (char *)block - 8;
        sf_footer *actual_footer = (sf_footer *) footer;
        size_t top_size = *actual_footer & SIZE;
        size_t current_size = block->header & SIZE;
        
        // Remove from free list
        char *top = (char *)block - top_size;
        removeFromFreeList((sf_block *)top);

        // Set new coalesce block
         sf_block *free;
        if((*footer & PREV_BLOCK_ALLOCATED) == 0) {
            free = setFreeBlock((sf_block *)top, top_size + current_size, 0);
        }
        else {
            free = setFreeBlock((sf_block *)top, top_size + current_size, 1);
        }
        
        addToFreeList(free);
        // sf_show_heap();
        return free;
    }


    // Top is allocated and bottom is free
    else if((block->header & PREV_BLOCK_ALLOCATED) != 0 && (bottom->header & THIS_BLOCK_ALLOCATED) == 0) {
        size_t current_size = block->header & SIZE;
        size_t bottom_size = bottom->header & SIZE;

        removeFromFreeList(bottom);
        // printf("BOTTOM SIZE: %ld\n", bottom_size);
        sf_block *free = setFreeBlock((sf_block *)block, current_size + bottom_size, 1);
        addToFreeList(free);
        return free;
    }

    // Top and bottom are allocated
    else {
        sf_block *free = setFreeBlock((sf_block *)block, block->header & SIZE, 1);
        addToFreeList(free);
        return free;

    }
}


void sf_free(void *pp) {
    // Pointer is null or not 8 byte aligned
    if(pp == NULL || (uintptr_t)pp % 8 != 0) {
        abort();
    }

    char *header = (char *)pp - 8;
    sf_block *block = (sf_block *)header;

    // Block size is < 32 or size is not a multiple of 8
    if((block->header & SIZE) < 32 || (block->header & SIZE) % 8 != 0) {
        abort();
    }

    // Header is before the start of the heap or footer of the block is after the end of the last block of the heap
    sf_footer *footer = (sf_footer *)block + (block->header & SIZE) - 8;
    if(block < (sf_block *)sf_mem_start() || footer > (sf_footer *)sf_mem_end()) {
        abort();
    }
    // Block bit is not allocated or quicklist bit is allocated 
    if((block->header & THIS_BLOCK_ALLOCATED) == 0 || (block->header & IN_QUICK_LIST) != 0)
        abort();

    // sf_show_heap();

    // Prev alloc bit is 0 but the prev block is allocated
    if((block->header & PREV_BLOCK_ALLOCATED) == 0) {
        sf_footer *prev_footer = (sf_footer *)block - 8;

        if((*prev_footer & THIS_BLOCK_ALLOCATED) != 0) {
            abort();
        }
    }

    // Check if the block size matches quick list
    size_t block_size = block->header & SIZE;
    int index = 0;

    while(index < NUM_QUICK_LISTS) {

        /*  Quick List:
    - sf_quick_lists[NUM_QUICK_LISTS] os size [20] contains pointers to the quick lists in LIFO fashion
    - sf_quick_lists[0] contains quick lists with size of MIN_BLOCK_SIZE
    - size of quicklist = MIN_BLOCK_SIZE + (index * 8)
    - contains length field with a max of 5 blocks in each quick list 
    - inserting into a quick list at capcity causes it to be flushed and the existing blocks in the quick list are removed from the quick list and added 
        to the main free list, after coalescing, if possible.
    */ 

        size_t quick_size = 32 + (index * 8);
        // Check if block size matches quick list size
        if(block_size == quick_size) {
            // Insert into quicklist
            
            // Quick list is full 
            if(sf_quick_lists[index].length == 5) {
                // Flush the quicklist and add to main free list
                sf_block *pointer = sf_quick_lists[index].first->body.links.next;
                int count = 0;
                while(count < 5) {
                    sf_block *next = pointer->body.links.next;
                    if((pointer->header & PREV_BLOCK_ALLOCATED) == 0) {
                        setFreeBlock(pointer, pointer->header & SIZE, 0);
                    }
                    else {
                        setFreeBlock(pointer, pointer->header & SIZE, 1);
                    }
                    
                    addToFreeList(pointer);

                    pointer = next;
                }
                sf_quick_lists[index].length = 0;
            }
            int prev = 0;
            if((block->header & PREV_BLOCK_ALLOCATED) == 0)
                prev = 0;
            else
                prev = 1;

            // Insert into quicklist
            // Change the block into a quick list block
            sf_block *new_block = block;
            new_block->header = block->header & SIZE;
            new_block->header |= IN_QUICK_LIST | THIS_BLOCK_ALLOCATED;
            
 

            if(prev == 0) {
                new_block->header &= ~PREV_BLOCK_ALLOCATED;
            }
            else {
                new_block->header |= PREV_BLOCK_ALLOCATED;
            }


            // Set next and prev pointers in quicklist
            sf_block *old_first = sf_quick_lists[index].first;
            sf_quick_lists[index].first = new_block;

            new_block->body.links.next = old_first;
            
            sf_quick_lists[index].length += 1;

            //Set the bottom block prev bit to 1
            char *bottom_ptr = (char *)new_block + (new_block->header & SIZE);
            sf_block *bottom = (sf_block *)bottom_ptr;
            bottom->header |= PREV_BLOCK_ALLOCATED;

            return;
        }
        index++;
    }

    // Add to the main free list
    if((block->header & PREV_BLOCK_ALLOCATED) == 0) {
        block = setFreeBlock(block, block->header & SIZE, 0);
    }

    else {
        block = setFreeBlock(block, block->header & SIZE, 1);
    }
    
    // sf_show_heap();
    block = coalesce(block);

    // Set the bottom block prev bit to 0
    sf_block * bottom = (sf_block *)((char *)block + (block->header & SIZE));
    bottom->header &= ~PREV_BLOCK_ALLOCATED; 

    // printf("BOTOTM BLOCK\n");
    // sf_show_block(bottom);
}

void *sf_realloc(void *pp, size_t rsize) {
    char *header = (char *)pp - 8;
    sf_block *block = (sf_block *)header;

     // Pointer is null or not 8 byte aligned
    if(pp == NULL || (uintptr_t)pp % 8 != 0) {
        sf_errno = EINVAL;
        return NULL;
    }

    // Block size is < 32 or size is not a multiple of 8
    if((block->header & SIZE) < 32 || (block->header & SIZE) % 8 != 0) {
        sf_errno = EINVAL;
        return NULL;
    }

    // Header is before the start of the heap or footer of the block is after the end of the last block of the heap
    sf_footer *footer = (sf_footer *)block + (block->header & SIZE) - 8;
    if(block < (sf_block *)sf_mem_start() || footer > (sf_footer *)sf_mem_end()) {
        sf_errno = EINVAL;
        return NULL;
    }

    // Block bit is not allocated or quicklist bit is allocated 
    if((block->header & THIS_BLOCK_ALLOCATED) == 0 || (block->header & IN_QUICK_LIST) != 0) {
        sf_errno = EINVAL;
        return NULL;
    }
        

    // sf_show_heap();

    // Prev alloc bit is 0 but the prev block is allocated
    if((block->header & PREV_BLOCK_ALLOCATED) == 0) {
        sf_footer *prev_footer = (sf_footer *)block - 8;

        if((*prev_footer & THIS_BLOCK_ALLOCATED) != 0) {
            sf_errno = EINVAL;
            return NULL;
        }
    }

    if(rsize == 0) {
        sf_block *block = (sf_block *)pp;
        if((block->header |= PREV_BLOCK_ALLOCATED) == 0) {
            setFreeBlock((sf_block *)pp, rsize, 0);
        }
        else {
            setFreeBlock((sf_block *)pp, rsize, 1);
        }
        
        return NULL;
    }

    // Calculate block size including needed padding
    size_t total_size;

    if(rsize < 32) {
        total_size = 32;
    }

    else {
        // Non-zero:
        total_size = rsize + 8;
        if(total_size % 8 != 0) {
            total_size = total_size + 8 - (total_size % 8);
        }
    }

    // Reallocating to larger size
    if((block->header & SIZE) - 8 < rsize) {
        void *pointer = sf_malloc(rsize);

        if(pointer == NULL) {
            return NULL;
        }

        memcpy(pointer, pp, (block->header & SIZE) - 8);

        sf_free(pp);
        return pointer;
    }

    // Reallocating to smaller size
    else if((block->header & SIZE) - 8 > rsize) {
        // Case 1: Splitting results in splinter
        if((block->header & SIZE) - total_size < 32) {
            return pp;
        }

        // Case 2: Split the blocks
        else if((block->header & SIZE) - total_size >= 32) {
            // Set free block
            char *new_free_pointer = (char *)block + total_size;
            sf_block *new_free = setFreeBlock((sf_block *)new_free_pointer, (block->header & SIZE) - total_size, 1);

            // Set alloc block
            setAllocBlock(block, total_size);

            new_free = coalesce(new_free);
            return pp;
        }

        return NULL;
    }
    
    return NULL;
}

void *sf_memalign(size_t size, size_t align) {
    if ((align & (align - 1)) != 0 || align < 8) {
        sf_errno = EINVAL;
        return NULL;
    }

    if(size == 0)
        return NULL;

    // Allocation not successfull
    sf_errno = ENOMEM;
    return NULL;
}
