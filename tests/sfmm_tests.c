#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#define TEST_TIMEOUT 15

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & ~0x7))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

/*
 * Assert that the free list with a specified index has the specified number of
 * blocks in it.
 */
void assert_free_list_size(int index, int size) {
    int cnt = 0;
    sf_block *bp = sf_free_list_heads[index].body.links.next;
    while(bp != &sf_free_list_heads[index]) {
	cnt++;
	bp = bp->body.links.next;
    }
    cr_assert_eq(cnt, size, "Free list %d has wrong number of free blocks (exp=%d, found=%d)",
		 index, size, cnt);
}

/*
 * Assert the total number of quick list blocks of a specified size.
 * If size == 0, then assert the total number of all quick list blocks.
 */
void assert_quick_list_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_QUICK_LISTS; i++) {
	sf_block *bp = sf_quick_lists[i].first;
	while(bp != NULL) {
	    if(size == 0 || size == (bp->header & ~0x7))
		cnt++;
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of quick list blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of quick list blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz = sizeof(int);
	int *x = sf_malloc(sz);
	// printf("TEST POINTER: %ls\n", x);
	// sf_show_free_lists();

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(4024, 1);
	assert_free_list_size(7, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	// We want to allocate up to exactly four pages, so there has to be space
	// for the header and the link pointers.
	void *x = sf_malloc(16336);
	cr_assert_not_null(x, "x is NULL!");
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	void *x = sf_malloc(86100);

	cr_assert_null(x, "x is not NULL!");
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(85976, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_quick, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 32, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_quick_list_block_count(0, 1);
	assert_quick_list_block_count(40, 1);
	assert_free_block_count(0, 1);
	assert_free_block_count(3952, 1);
	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 200, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 2);
	assert_free_block_count(208, 1);
	assert_free_block_count(3784, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
	/* void *w = */ sf_malloc(sz_w);
	void *x = sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);
	sf_free(x);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 2);
	assert_free_block_count(520, 1);
	assert_free_block_count(3472, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
        size_t sz_u = 200, sz_v = 300, sz_w = 200, sz_x = 500, sz_y = 200, sz_z = 700;
	void *u = sf_malloc(sz_u);
	/* void *v = */ sf_malloc(sz_v);
	void *w = sf_malloc(sz_w);
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 4);
	assert_free_block_count(208, 3);
	assert_free_block_count(1896, 1);
	assert_free_list_size(3, 3);
	assert_free_list_size(6, 1);
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int), sz_y = 10, sz_x1 = sizeof(int) * 20;
	void *x = sf_malloc(sz_x);
	/* void *y = */ sf_malloc(sz_y);
	x = sf_realloc(x, sz_x1);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x7) == 88, "Realloc'ed block size not what was expected!");

	assert_quick_list_block_count(0, 1);
	assert_quick_list_block_count(32, 1);
	assert_free_block_count(0, 1);
	assert_free_block_count(3904, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int) * 20, sz_y = sizeof(int) * 16;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char *)y - sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x7) == 88, "Realloc'ed block size not what was expected!");

	// There should be only one free block.
	assert_quick_list_block_count(0, 0);
	assert_free_block_count(0, 1);
	assert_free_block_count(3968, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(double) * 8, sz_y = sizeof(int);
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char *)y - sizeof(sf_header));
	cr_assert(bp->header & THIS_BLOCK_ALLOCATED, "Allocated bit is not set!");
	cr_assert((bp->header & ~0x7) == 32, "Realloc'ed block size not what was expected!");

	// After realloc'ing x, we can return a block of size ADJUSTED_BLOCK_SIZE(sz_x) - ADJUSTED_BLOCK_SIZE(sz_y)
	// to the freelist.  This block will go into the main freelist and be coalesced.
	// Note that we don't put split blocks into the quick lists because their sizes are not sizes
	// that were requested by the client, so they are not very likely to satisfy a new request.
	assert_quick_list_block_count(0, 0);	
	assert_free_block_count(0, 1);
	assert_free_block_count(4024, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

//Test(sfmm_student_suite, student_test_1, .timeout = TEST_TIMEOUT) {
//}



////////////////////////////////////////////////// GRADING HELPERS //////////////////////////////////////////////////////////
#include "__grading_helpers.h"
#include "debug.h"
#include "sfmm.h"

int free_list_idx(size_t size) {
    size_t s = 32;
    int i = 0;
    while(i < NUM_FREE_LISTS-1) {
        if(size <= s)
            return i;
        i++;
	s *= 2;
    }
    return NUM_FREE_LISTS-1;
}

static bool free_list_is_empty()
{
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	if(sf_free_list_heads[i].body.links.next != &sf_free_list_heads[i] ||
	   sf_free_list_heads[i].body.links.prev != &sf_free_list_heads[i])
	    return false;
   }
   return true;
}

static bool block_is_in_free_list(sf_block *abp)
{
    sf_block *bp = NULL;
    size_t size = abp->header & ~0x7;
    int i = free_list_idx(size);
    for(bp = sf_free_list_heads[i].body.links.next; bp != &sf_free_list_heads[i];
        bp = bp->body.links.next) {
            if (bp == abp)  return true;
    }
    return false;
}

void _assert_free_list_is_empty()
{
    cr_assert(free_list_is_empty(), "Free list is not empty");
}

/*
 * Function checks if all blocks are unallocated in free_list.
 * This function does not check if the block belongs in the specified free_list.
 */
void _assert_free_list_is_valid()
{
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_block *bp = sf_free_list_heads[i].body.links.next;
        int limit = 10000;
        while(bp != &sf_free_list_heads[i] && limit--) {
            cr_assert(!(bp->header & THIS_BLOCK_ALLOCATED),
		      "Block %p in free list is marked allocated", &(bp)->header);
            bp = bp->body.links.next;
        }
        cr_assert(limit != 0, "Bad free list");
    }
}

/**
 * Checks if a block follows documentation constraints
 *
 * @param bp pointer to the block to check
 */
void _assert_block_is_valid(sf_block *bp)
{
   // proper block alignment & size
   size_t unaligned_payload = ((size_t)(&bp->body.payload) & 0x7);
   cr_assert(!unaligned_payload,
      "Block %p is not properly aligned", &(bp)->header);
    size_t block_size = bp->header & ~0x7;

   cr_assert(block_size >= 32 && block_size <= 409600 && !(block_size & 0x7),
      "Block size is invalid for %p. Got: %lu", &(bp)->header, block_size);

   // prev alloc bit of next block == alloc bit of this block
   sf_block *nbp = (sf_block *)((char *)bp + block_size);

   cr_assert(nbp == ((sf_block *)(sf_mem_end() - 8)) ||
	     (((nbp->header & PREV_BLOCK_ALLOCATED) != 0) == ((bp->header & THIS_BLOCK_ALLOCATED) != 0)),
	     "Prev allocated bit is not correctly set for %p. Should be: %d",
	     &(nbp)->header, bp->header & THIS_BLOCK_ALLOCATED);

   // other issues to check
   sf_footer *footer = (sf_footer *)((char *)(nbp) - sizeof(sf_footer));
   cr_assert((bp->header & THIS_BLOCK_ALLOCATED) || (*footer == bp->header),
	     "block's footer does not match header for %p", &(bp)->header);
   if (bp->header & THIS_BLOCK_ALLOCATED) {
       cr_assert(!block_is_in_free_list(bp),
		 "Allocated block at %p is also in the free list", &(bp)->header);
   } else {
       cr_assert(block_is_in_free_list(bp),
		 "Free block at %p is not contained in the free list", &(bp)->header);
   }
}

void _assert_heap_is_valid(void)
{
    sf_block *bp;
    int limit = 10000;

    if(sf_mem_start() == sf_mem_end())
        cr_assert(free_list_is_empty(), "The heap is empty, but the free list is not");
    for(bp = ((sf_block *)(sf_mem_start() + 32)); limit-- && bp < ((sf_block *)(sf_mem_end() - 8));
	bp = (sf_block *)((char *)bp + (bp->header & ~0x7)))
	_assert_block_is_valid(bp);
    cr_assert(bp == ((sf_block *)(sf_mem_end() - 8)), "Could not traverse entire heap");
    _assert_free_list_is_valid();
}

/**
 * Asserts a block's info.
 *
 * @param bp pointer to the beginning of the block.
 * @param alloc The expected allocation bit for the block.
 * @param b_size The expected block size.
 */
void _assert_block_info(sf_block *bp, int alloc, size_t b_size)
{
    cr_assert((bp->header & THIS_BLOCK_ALLOCATED) == alloc,
	      "Block %p has wrong allocation status (got %d, expected %d)",
	      &(bp)->header, bp->header & THIS_BLOCK_ALLOCATED, alloc);

    cr_assert((bp->header & ~0x7) == b_size,
	      "Block %p has wrong block_size (got %lu, expected %lu)",
	      &(bp)->header, (bp->header & ~0x7), b_size);
}

/**
 * Asserts payload pointer is not null.
 *
 * @param pp payload pointer.
 */
void _assert_nonnull_payload_pointer(void *pp)
{
    cr_assert(pp != NULL, "Payload pointer should not be NULL");
}

/**
 * Asserts payload pointer is null.
 *
 * @param pp payload pointer.
 */
void _assert_null_payload_pointer(void * pp)
{
    cr_assert(pp == NULL, "Payload pointer should be NULL");
}

/**
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 *
 * @param size the size of free blocks to count.
 * @param count the expected number of free blocks to be counted.
 */
void _assert_free_block_count(size_t size, int count)
{
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_block *bp = sf_free_list_heads[i].body.links.next;
        while(bp != &sf_free_list_heads[i]) {
            if(size == 0 || size == (bp->header & ~0x7))
                cnt++;
            bp = bp->body.links.next;
        }
    }
    if(size)
        cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    else
        cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
}


/*
 * Assert the total number of quick list blocks of a specified size.
 * If size == 0, then assert the total number of all quick list blocks.
 */
void _assert_quick_list_block_count(size_t size, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_QUICK_LISTS; i++) {
        sf_block *bp = sf_quick_lists[i].first;
        while(bp != NULL) {
            if(size == 0 || size == (bp->header & ~0x7))
                cnt++;
            bp = bp->body.links.next;
        }
    }
    if(size == 0) {
    cr_assert_eq(cnt, count, "Wrong number of quick list blocks (exp=%d, found=%d)",
             count, cnt);
    } else {
    cr_assert_eq(cnt, count, "Wrong number of quick list blocks of size %ld (exp=%d, found=%d)",
             size, count, cnt);
    }
}

/**
 * Assert the sf_errno.
 *
 * @param n the errno expected
 */
void _assert_errno_eq(int n)
{
    cr_assert_eq(sf_errno, n, "sf_errno has incorrect value (value=%d, exp=%d)", sf_errno, n);
}


//////////////////////////////////////////////////// GRADING TESTS P1 ////////////////////////////////////////////////////
#define TEST_TIMEOUT 15
#include "__grading_helpers.h"
#include <assert.h>

/*
 * Do one malloc and check that the prologue and epilogue are correctly initialized
 */
Test(sf_memsuite_grading, initialization, .timeout = TEST_TIMEOUT)
{
	size_t sz = 1;
	void *p  = sf_malloc(sz);
	cr_assert(p != NULL, "The pointer should NOT be null after a malloc");
	_assert_heap_is_valid();
}

/*
 * Single malloc tests, up to the size that forces a non-minimum block size.
 */
Test(sf_memsuite_grading, single_malloc_1, .timeout = TEST_TIMEOUT)
{

	size_t sz = 1;
	void *x = sf_malloc(sz);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 32);
	_assert_heap_is_valid();
	_assert_free_block_count(4024, 1);
	_assert_quick_list_block_count(0, 0);

	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, single_malloc_16, .timeout = TEST_TIMEOUT)
{
	size_t sz = 16;
	void *x = sf_malloc(sz);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 32);
	_assert_heap_is_valid();
	_assert_free_block_count(4024, 1);
	_assert_quick_list_block_count(0, 0);

	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, single_malloc_32, .timeout = TEST_TIMEOUT)
{
	size_t sz = 32;
	void *x = sf_malloc(sz);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 40);
	_assert_heap_is_valid();
	_assert_free_block_count(4016, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_errno_eq(0);
}


/*
 * Single malloc test, of a size exactly equal to what is left after initialization.
 * Requesting the exact remaining size (leaving space for the header)
 */
Test(sf_memsuite_grading, single_malloc_exactly_one_page_needed, .timeout = TEST_TIMEOUT)
{
	void *x = sf_malloc(4048);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 4056);
	_assert_heap_is_valid();
	_assert_free_block_count(0, 0);
	_assert_quick_list_block_count(0, 0);
	_assert_errno_eq(0);
}

/*
 * Single malloc test, of a size just larger than what is left after initialization.
 */
Test(sf_memsuite_grading, single_malloc_more_than_one_page_needed, .timeout = TEST_TIMEOUT)
{
	size_t sz = 4056;
	void *x = sf_malloc(sz);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 4064);
	_assert_heap_is_valid();	
	_assert_free_block_count(4088, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_errno_eq(0);
}

/*
 * Single malloc test, of multiple pages.
 */
Test(sf_memsuite_grading, single_malloc_three_pages_needed, .timeout = TEST_TIMEOUT)
{
	size_t sz = (size_t)((int)(PAGE_SZ * 3 / 1000) * 1000);
	void *x = sf_malloc(sz);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 12008);
	_assert_heap_is_valid();
	_assert_free_block_count(240, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_errno_eq(0);
}

/*
 * Single malloc test, unsatisfiable.
 * There should be one single large block.
 */
Test(sf_memsuite_grading, single_malloc_max, .timeout = TEST_TIMEOUT)
{
	void *x = sf_malloc(86016);
	_assert_null_payload_pointer(x);
	_assert_heap_is_valid();
	_assert_free_block_count(85976, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_errno_eq(ENOMEM);
}

/*
 * Malloc/free with/without coalescing.
 */
Test(sf_memsuite_grading, malloc_free_no_coalesce, .timeout = TEST_TIMEOUT)
{
        size_t sz1 = 200;
	size_t sz2 = 300;
	size_t sz3 = 400;

	void *x = sf_malloc(sz1);
	_assert_nonnull_payload_pointer(x);
	void *y = sf_malloc(sz2);
	_assert_nonnull_payload_pointer(y);
	void *z = sf_malloc(sz3);
	_assert_nonnull_payload_pointer(z);
	sf_free(y);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);
	_assert_block_info((sf_block *)((char *)y - 8), 0, 312);
	_assert_block_info((sf_block *)((char *)z - 8), 1, 408);
	_assert_heap_is_valid();

	_assert_free_block_count(312, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_free_block_count(3128, 1);
	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, malloc_free_coalesce_lower, .timeout = TEST_TIMEOUT)
{
	size_t sz1 = 200;
	size_t sz2 = 300;
	size_t sz3 = 400;
	size_t sz4 = 500;

	void *x = sf_malloc(sz1);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);

	void *y = sf_malloc(sz2);
	_assert_nonnull_payload_pointer(y);
	_assert_block_info((sf_block *)((char *)y - 8), 1, 312);

	void *z = sf_malloc(sz3);
	_assert_nonnull_payload_pointer(z);
	_assert_block_info((sf_block *)((char *)z - 8), 1, 408);

	void *w = sf_malloc(sz4);
	_assert_nonnull_payload_pointer(w);
	_assert_block_info((sf_block *)((char *)w - 8), 1, 512);

	sf_free(y);
	sf_free(z);

	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);
	_assert_block_info((sf_block *)((char *)y - 8), 0, 720);
	_assert_block_info((sf_block *)((char *)w - 8), 1, 512);
	_assert_heap_is_valid();

	_assert_free_block_count(720, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_free_block_count(2616, 1);

	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, malloc_free_coalesce_upper, .timeout = TEST_TIMEOUT)
{
	size_t sz1 = 200;
	size_t sz2 = 300;
	size_t sz3 = 400;
	size_t sz4 = 500;

	void *x = sf_malloc(sz1);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);

	void *y = sf_malloc(sz2);
	_assert_nonnull_payload_pointer(y);
	_assert_block_info((sf_block *)((char *)y - 8), 1, 312);

	void *z = sf_malloc(sz3);
	_assert_nonnull_payload_pointer(z);
	_assert_block_info((sf_block *)((char *)z - 8), 1, 408);

	void *w = sf_malloc(sz4);
	_assert_nonnull_payload_pointer(w);
	_assert_block_info((sf_block *)((char *)w - 8), 1, 512);

	sf_free(z);
	sf_free(y);

	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);
	_assert_block_info((sf_block *)((char *)y - 8), 0, 720);
	_assert_block_info((sf_block *)((char *)w - 8), 1, 512);
	_assert_heap_is_valid();
	_assert_free_block_count(720, 1);
	_assert_quick_list_block_count(0, 0);

	_assert_free_block_count(2616, 1);

	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, malloc_free_coalesce_both, .timeout = TEST_TIMEOUT)
{
	size_t sz1 = 200;
	size_t sz2 = 300;
	size_t sz3 = 400;
	size_t sz4 = 500;

	void *x = sf_malloc(sz1);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);

	void *y = sf_malloc(sz2);
	_assert_nonnull_payload_pointer(y);
	_assert_block_info((sf_block *)((char *)y - 8), 1, 312);

	void *z = sf_malloc(sz3);
	_assert_nonnull_payload_pointer(z);
	_assert_block_info((sf_block *)((char *)z - 8), 1, 408);

	void *w = sf_malloc(sz4);
	_assert_nonnull_payload_pointer(w);
	_assert_block_info((sf_block *)((char *)w - 8), 1, 512);

	sf_free(x);
	sf_free(z);
	sf_free(y);

	_assert_block_info((sf_block *)((char *)x - 8), 0, 928);
	_assert_heap_is_valid();
	_assert_free_block_count(928, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_free_block_count(2616, 1);

	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, malloc_free_first_block, .timeout = TEST_TIMEOUT)
{
	size_t sz1 = 200;
	size_t sz2 = 300;

	void *x = sf_malloc(sz1);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);

	void *y = sf_malloc(sz2);
	_assert_nonnull_payload_pointer(y);
	_assert_block_info((sf_block *)((char *)y - 8), 1, 312);

	sf_free(x);

	_assert_block_info((sf_block *)((char *)x - 8), 0, 208);
	_assert_block_info((sf_block *)((char *)y - 8), 1, 312);
	_assert_heap_is_valid();

	_assert_free_block_count(208, 1);
	_assert_quick_list_block_count(0, 0);
	_assert_free_block_count(3536, 1);

	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, malloc_free_last_block, .timeout = TEST_TIMEOUT)
{
	size_t sz1 = 200;
	size_t sz2 = 3840;
	void *x = sf_malloc(sz1);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);

	void *y = sf_malloc(sz2);
	_assert_nonnull_payload_pointer(y);
	_assert_block_info((sf_block *)((char *)y - 8), 1, 3848);

	sf_free(y);

	_assert_block_info((sf_block *)((char *)x - 8), 1, 208);
	_assert_block_info((sf_block *)((char *)y - 8), 0, 3848);
	_assert_free_block_count(3848, 1);
	_assert_quick_list_block_count(0, 0);

	_assert_heap_is_valid();

	_assert_errno_eq(0);
}

/*
 * Check that malloc leaves no splinter.
 */
Test(sf_memsuite_grading, malloc_with_splinter, .timeout = TEST_TIMEOUT)
{
	void *x = sf_malloc(4025);
	_assert_nonnull_payload_pointer(x);
	_assert_block_info((sf_block *)((char *)x - 8), 1, 4056);
	_assert_heap_is_valid();

	_assert_free_block_count(0, 0);
	_assert_quick_list_block_count(0, 0);

	_assert_errno_eq(0);
}

/*
 * Determine if the existing heap can satisfy an allocation request
 * of a specified size.  The heap blocks are examined directly;
 * freelists are ignored.
 */
static int can_satisfy_request(size_t size) {
    size_t asize = 112;
	if(asize < 32)
        asize = 32;
    sf_block *bp = ((sf_block *)(sf_mem_start() + 32));
    while(bp < (sf_block *)(sf_mem_end() - 8)) {
	if(!(bp->header & THIS_BLOCK_ALLOCATED) && asize <= (bp->header & ~0x7))
	    return 1;
	bp = (sf_block *)((char *)bp + (bp->header & ~0x7));
    }
    return 0;
}

/*
 *  Allocate small blocks until memory exhausted.
 */
Test(sf_memsuite_grading, malloc_to_exhaustion, .timeout = TEST_TIMEOUT)
{
    size_t size = 90;  // Arbitrarily chosen small size.
    size_t asize = 104;
    int limit = 10000;

    void *x;
    size_t bsize = 0;
    while(limit > 0 && (x = sf_malloc(size)) != NULL) {
	sf_block *bp = (sf_block *)((char *)x - 8);
	bsize = (bp->header & ~0x7);
	cr_assert(!bsize || bsize - asize < 32,
		  "Block has incorrect size (was: %lu, exp range: [%lu, %lu))",
		  bsize, asize, asize + 32);
	limit--;
    }
    cr_assert_null(x, "Allocation succeeded, but heap should be exhausted.");
    _assert_errno_eq(ENOMEM);
    cr_assert_null(sf_mem_grow(), "Allocation failed, but heap can still grow.");
    cr_assert(!can_satisfy_request(size),
	      "Allocation failed, but there is still a suitable free block.");
}

/*
 *  Test sf_memalign handling invalid arguments:
 *  If align is not a power of two or is less than the minimum block size,
 *  then NULL is returned and sf_errno is set to EINVAL.
 *  If size is 0, then NULL is returned without setting sf_errno.
 */
Test(sf_memsuite_grading, sf_memalign_test_1, .timeout = TEST_TIMEOUT)
{
    /* Test size is 0, then NULL is returned without setting sf_errno */
    int old_errno = sf_errno;  // Save the current errno just in case
    sf_errno = ENOTTY;  // Set errno to something it will never be set to as a test (in this case "not a typewriter")
    size_t arg_align = 32;
    size_t arg_size = 0;
    void *actual_ptr = sf_memalign(arg_size, arg_align);
    cr_assert_null(actual_ptr, "sf_memalign didn't return NULL when passed a size of 0");
    _assert_errno_eq(ENOTTY);  // Assert that the errno didn't change
    sf_errno = old_errno;  // Restore the old errno

    /* Test align less than the minimum block size */
    arg_align = 1U << 2;  // A power of 2 that is still less than MIN_BLOCK_SIZE
    arg_size = 25;  // Arbitrary
    actual_ptr = sf_memalign(arg_size, arg_align);
    cr_assert_null(actual_ptr, "sf_memalign didn't return NULL when passed align that was less than the minimum block size");
    _assert_errno_eq(EINVAL);

    /* Test align that isn't a power of 2 */
    arg_align = (32 << 1) - 1;  // Greater than 32, but not a power of 2
    arg_size = 65;  // Arbitrary
    actual_ptr = sf_memalign(arg_size, arg_align);
    cr_assert_null(actual_ptr, "sf_memalign didn't return NULL when passed align that wasn't a power of 2");
    _assert_errno_eq(EINVAL);
}

/*
Test that memalign returns an aligned address - using minimum block size for alignment
 */
Test(sf_memsuite_grading, sf_memalign_test_2, .timeout = TEST_TIMEOUT)
{
    size_t arg_align = 32;
    size_t arg_size = 25;  // Arbitrary
    void *x = sf_memalign(arg_size, arg_align);
    _assert_nonnull_payload_pointer(x);
    if (((unsigned long)x & (arg_align-1)) != 0) {
        cr_assert(1 == 0, "sf_memalign didn't return an aligned address!");
    }
}

/*
Test that memalign returns aligned, usable memory
 */
Test(sf_memsuite_grading, sf_memalign_test_3, .timeout = TEST_TIMEOUT)
{
    size_t arg_align = 32<<1; // Use larger than minimum block size for alignment
    size_t arg_size = 129;  // Arbitrary
    void *x = sf_memalign(arg_size, arg_align);
    _assert_nonnull_payload_pointer(x);
    if (((unsigned long)x & (arg_align-1)) != 0) {
        cr_assert(1 == 0, "sf_memalign didn't return an aligned address!");
    }
    _assert_heap_is_valid();
}

// Quick list tests
Test(sf_memsuite_grading, simple_quick_list, .timeout = TEST_TIMEOUT)
{

	size_t sz = 32;
	void *x = sf_malloc(sz);
	sf_free(x);  // Goes to quick list

	_assert_quick_list_block_count(0, 1);
	_assert_errno_eq(0);
}

Test(sf_memsuite_grading, multiple_quick_lists, .timeout = TEST_TIMEOUT)
{
	void *a = sf_malloc(32);
	void *b = sf_malloc(48);
	void *c = sf_malloc(96);
	void *d = sf_malloc(112);
	void *e = sf_malloc(128);

	sf_free(a);  // Goes to quick list
	sf_free(b);
	sf_free(c);
	sf_free(d);
	sf_free(e);

	_assert_quick_list_block_count(0, 5);
	_assert_errno_eq(0);
}


/////////////////////////////////////////////////// GRADING TESTS P2 //////////////////////////////////////////////
#define TEST_TIMEOUT 15
#include "__grading_helpers.h"
#include "debug.h"

/*
 * Check LIFO discipline on free list
 */
Test(sf_memsuite_grading, malloc_free_lifo, .timeout=TEST_TIMEOUT)
{
    size_t sz = 200;
    void * x = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x);
    _assert_block_info((sf_block *)((char *)x - 8), 1, 208);
    void * u = sf_malloc(sz);
    _assert_nonnull_payload_pointer(u);
    _assert_block_info((sf_block *)((char *)u - 8), 1, 208);
    void * y = sf_malloc(sz);
    _assert_nonnull_payload_pointer(y);
    _assert_block_info((sf_block *)((char *)y - 8), 1, 208);
    void * v = sf_malloc(sz);
    _assert_nonnull_payload_pointer(v);
    _assert_block_info((sf_block *)((char *)v - 8), 1, 208);
    void * z = sf_malloc(sz);
    _assert_nonnull_payload_pointer(z);
    _assert_block_info((sf_block *)((char *)z - 8), 1, 208);
    void * w = sf_malloc(sz);
    _assert_nonnull_payload_pointer(w);
    _assert_block_info((sf_block *)((char *)w - 8), 1, 208);

    sf_free(x);
    sf_free(y);
    sf_free(z);

    void * z1 = sf_malloc(sz);
    _assert_nonnull_payload_pointer(z1);
    _assert_block_info((sf_block *)((char *)z1 - 8), 1, 208);
    void * y1 = sf_malloc(sz);
    _assert_nonnull_payload_pointer(y1);
    _assert_block_info((sf_block *)((char *)y1 - 8), 1, 208);
    void * x1 = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x1);
    _assert_block_info((sf_block *)((char *)x1 - 8), 1, 208);

    cr_assert(x == x1 && y == y1 && z == z1,
      "malloc/free does not follow LIFO discipline");

    _assert_free_block_count(2808, 1);
    _assert_quick_list_block_count(0, 0);

    _assert_errno_eq(0);
}

/*
 * Realloc tests.
 */
Test(sf_memsuite_grading, realloc_larger, .timeout=TEST_TIMEOUT)
{
    size_t sz = 200;
    size_t nsz = 1024;

    void * x = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x);
    _assert_block_info((sf_block *)((char *)x - 8), 1, 208);

    void * y = sf_realloc(x, nsz);
    _assert_nonnull_payload_pointer(y);
    _assert_block_info((sf_block *)((char *)y - 8), 1, 1032);

    _assert_free_block_count(208, 1);
    _assert_quick_list_block_count(0, 0);
    _assert_free_block_count(2816, 1);

    _assert_errno_eq(0);
}

Test(sf_memsuite_grading, realloc_smaller, .timeout=TEST_TIMEOUT)
{
    size_t sz = 1024;
    size_t nsz = 200;

    void * x = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x);
    _assert_block_info((sf_block *)((char *)x - 8), 1, 1032);

    void * y = sf_realloc(x, nsz);
    _assert_nonnull_payload_pointer(y);
    _assert_block_info((sf_block *)((char *)y - 8), 1, 208);

    cr_assert_eq(x, y, "realloc to smaller size did not return same payload pointer");

    _assert_free_block_count(3848, 1);
    _assert_quick_list_block_count(0, 0);
    _assert_errno_eq(0);
}

Test(sf_memsuite_grading, realloc_same, .timeout=TEST_TIMEOUT)
{
    size_t sz = 1024;
    size_t nsz = 1024;

    void * x = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x);
    _assert_block_info((sf_block *)((char *)x - 8), 1, 1032);

    void * y = sf_realloc(x, nsz);
    _assert_nonnull_payload_pointer(y);
    _assert_block_info((sf_block *)((char *)y - 8), 1, 1032);

    cr_assert_eq(x, y, "realloc to same size did not return same payload pointer");
    _assert_free_block_count(3024, 1);

    _assert_errno_eq(0);
}

Test(sf_memsuite_grading, realloc_splinter, .timeout=TEST_TIMEOUT)
{
    size_t sz = 1024;
    size_t nsz = 1020;

    void * x = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x);
    _assert_block_info((sf_block *)((char *)x - 8), 1, 1032);

    void * y = sf_realloc(x, nsz);
    _assert_nonnull_payload_pointer(y);
    _assert_block_info((sf_block *)((char *)y - 8), 1, 1032);

    cr_assert_eq(x, y, "realloc to smaller size did not return same payload pointer");

    _assert_free_block_count(3024, 1);
    _assert_errno_eq(0);
}

Test(sf_memsuite_grading, realloc_size_0, .timeout=TEST_TIMEOUT)
{
    size_t sz = 1024;
    void * x = sf_malloc(sz);
    _assert_nonnull_payload_pointer(x);
    _assert_block_info((sf_block *)((char *)x - 8), 1, 1032);

    void * y = sf_malloc(sz);
    _assert_nonnull_payload_pointer(y);
    _assert_block_info((sf_block *)((char *)y - 8), 1, 1032);

    void * z = sf_realloc(x, 0);
    _assert_null_payload_pointer(z);
    _assert_block_info((sf_block *)((char *)x - 8), 0, 1032);

    // after realloc x to (2) z, x is now a free block
    size_t exp_free_sz_x2z = 1032;
    _assert_free_block_count(exp_free_sz_x2z, 1);

    _assert_free_block_count(1992, 1);

    _assert_errno_eq(0);
}

/*
 * Illegal pointer tests.
 */
Test(sf_memsuite_grading, free_null, .signal = SIGABRT, .timeout = TEST_TIMEOUT)
{
    size_t sz = 1;
    (void) sf_malloc(sz);
    sf_free(NULL);
    cr_assert_fail("SIGABRT should have been received");
}

//This test tests: Freeing a memory that was free-ed already
Test(sf_memsuite_grading, free_unallocated, .signal = SIGABRT, .timeout = TEST_TIMEOUT)
{
    size_t sz = 1;
    void *x = sf_malloc(sz);
    sf_free(x);
    sf_free(x);
    cr_assert_fail("SIGABRT should have been received");
}

Test(sf_memsuite_grading, free_block_too_small, .signal = SIGABRT, .timeout = TEST_TIMEOUT)
{
    size_t sz = 1;
    void * x = sf_malloc(sz);
    *((char *)x - 8) = 0x0UL;
    sf_free(x);
    cr_assert_fail("SIGABRT should have been received");
}


// random block assigments. Tried to give equal opportunity for each possible order to appear.
// But if the heap gets populated too quickly, try to make some space by realloc(half) existing
// allocated blocks.
Test(sf_memsuite_grading, stress_test, .timeout = TEST_TIMEOUT)
{
    errno = 0;

    int order_range = 13;
    int nullcount = 0;

    void * tracked[100];

    for (int i = 0; i < 100; i++)
    {
        int order = (rand() % order_range);
        size_t extra = (rand() % (1 << order));
        size_t req_sz = (1 << order) + extra;

        tracked[i] = sf_malloc(req_sz);
        // if there is no free to malloc
        if (tracked[i] == NULL)
        {
            order--;
            while (order >= 0)
            {
                req_sz = (1 << order) + (extra % (1 << order));
                tracked[i] = sf_malloc(req_sz);
                if (tracked[i] != NULL)
                {
                    break;
                }
                else
                {
                    order--;
                }
            }
        }

        // tracked[i] can still be NULL
        if (tracked[i] == NULL)
        {
            nullcount++;
            // It seems like there is not enough space in the heap.
            // Try to halve the size of each existing allocated block in the heap,
            // so that next mallocs possibly get free blocks.
            for (int j = 0; j < i; j++)
            {
                if (tracked[j] == NULL)
                {
                    continue;
                }
                sf_block * bp = (sf_block *)((char *)tracked[j] - 8);
                req_sz = (bp->header & ~0x7) >> 1;
                tracked[j] = sf_realloc(tracked[j], req_sz);
            }
        }
        errno = 0;
    }

    for (int i = 0; i < 100; i++)
    {
        if (tracked[i] != NULL)
        {
            sf_free(tracked[i]);
        }
    }

    _assert_heap_is_valid();

    // As allocations are random, there is a small probability that the entire heap
    // has not been used.  So only assert that there is one free block, not what size it is.
    //size_t exp_free_sz = MAX_SIZE - sizeof(_sf_prologue) - sizeof(_sf_epilogue);
    /*
     * We can't assert that there's only one big free block anymore because of
     * the quick lists. Blocks inside quick lists are still marked "allocated" so
     * they won't be coalesced with adjacent blocks.
     *
     * Let's comment this out. After all, _assert_heap_is_valid() will check uncoalesced
     * adjacent free blocks anyway.
     */
    // _assert_free_block_count(0, 1);
}
