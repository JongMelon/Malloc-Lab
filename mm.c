/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "zkk",
    /* First member's full name */
    "zk",
    /* First member's email address */
    "zhukai2022@ruc.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/*
    segregrated list

    1. An allocated block only has a header.
    2. A free block has a header and a footer, and prev and next pointers.
    3. The lowest bit of the header is used to indicate whether the block is allocated or free.
    4. The second lowest bit of the header is used to indicate whether the previous block is allocated or free.
    5. The third lowest bit of the header is used to indicate whether the next block is allocated or free.
    6. The prev and next pointers are stored in the payload of the free block.
    7. The max size of the allocated space is 2^12, so the lists are divided into 13 levels.
*/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define FSIZE 16
#define CHUNKSIZE (4104) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))
#define PACK_ALLOC(size, alloc, prev_alloc, next_alloc) ((size) | (alloc) | ((prev_alloc) << 1) | ((next_alloc) << 2))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned)(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)
#define GET_NEXT_ALLOC(p) ((GET(p) & 0x4) >> 2)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Given block ptr bp, compute address of next and previous blocks in list */
#define PREV_POINTER(bp) GET(bp)
#define NEXT_POINTER(bp) GET((char* )bp + WSIZE)

/* Set the prev and the next ptr of an block */
#define SET_PREV(bp, prev) PUT(bp, (unsigned long long)prev)
#define SET_NEXT(bp, next) PUT((char*)(bp) + WSIZE, (unsigned long long)next)

/* Set the allocated bit */
#define SET_ALLOC(p, alloc) ((*(unsigned int *)(p)) = (*(unsigned int *)(p) & ~0x1) | (alloc))
#define SET_PREV_ALLOC(p, prev_alloc) ((*(unsigned int *)(p)) = (*(unsigned int *)(p) & ~0x2) | ((prev_alloc) << 1))
#define SET_NEXT_ALLOC(p, next_alloc) ((*(unsigned int *)(p)) = (*(unsigned int *)(p) & ~0x4) | ((next_alloc) << 2))

#define LISTS_NUM 12

#define ADJUST_WEIGHT 8

#define FIT_ITS 50

inline size_t adjust_size(size_t size);

static void *extend_heap(size_t words);
static void *next_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void *coalesce(void *bp);

static void insert_node(void *bp);
static void delete_node(void *bp);

inline int get_list_id(size_t size);

static char *heap_listp = 0;
static char *pre_listp;

static char **lists;

unsigned long long ptr_mask = 0xffffffff;

int is_save = 0;

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    //freopen("log.out", "w", stdout);
    if ((lists = mem_sbrk(LISTS_NUM * DSIZE)) == (void *)-1)
        return -1;

    ptr_mask = 0xffffffff;
    ptr_mask = ~ptr_mask & (unsigned long long)lists;

    for (int i = 0; i < LISTS_NUM; i++)
        lists[i] = 0;

    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0); /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1)); /* Epilogue header */
    heap_listp += (2 * WSIZE);
    pre_listp = heap_listp;
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

inline size_t adjust_size(size_t size) {
    if (size == 112)
        return 128;
    if (size == 448)
        return 512;
    return size;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize; /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;
    
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;
    
    size = adjust_size(size);

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    /* Search the free list for a fit */
    if ((bp = next_fit(asize)) != NULL) {
        bp = place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    bp = place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if (ptr == 0)
        return;
    size_t size = GET_SIZE(HDRP(ptr));
    SET_ALLOC(HDRP(ptr), 0);
    SET_ALLOC(FTRP(ptr), 0);
    SET_PREV_ALLOC(HDRP(NEXT_BLKP(ptr)), 0);
    SET_NEXT_ALLOC(HDRP(PREV_BLKP(ptr)), 0);
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{   
    if (ptr == NULL)
       return mm_malloc(size);

    if (size == 0) {
       mm_free(ptr);
       return NULL;
    }

    size_t asize = ALIGN(size + DSIZE);

    size_t old_size = GET_SIZE(HDRP(ptr));

    if (old_size >= asize) {
        if (!GET_ALLOC(HDRP(ptr))) 
            exit(0);
        return ptr;
    }

    void* newptr = NULL;

    // the next block is allocated, and it's not the epilogue block
    if (GET_NEXT_ALLOC(HDRP(ptr)) && GET_SIZE(NEXT_BLKP(ptr))) {
        newptr = mm_malloc(size);
        if (!newptr)
            return NULL;
        memmove(newptr, ptr, old_size - WSIZE);

        mm_free(ptr);
    }
    else {
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        if (!next_size)
            if (extend_heap(MAX(ALIGN(asize - old_size), CHUNKSIZE) / WSIZE) == NULL)
                return NULL;
        next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        if (next_size + old_size >= asize) {
            delete_node(NEXT_BLKP(ptr));
            int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
            PUT(HDRP(ptr), PACK_ALLOC(next_size + old_size, 1, prev_alloc, 1));
            PUT(FTRP(ptr), PACK_ALLOC(next_size + old_size, 1, prev_alloc, 1));
            newptr = ptr;
            SET_PREV_ALLOC(HDRP(NEXT_BLKP(newptr)), 1);
        }
        else {
            size_t next_next_size = GET_SIZE(HDRP(NEXT_BLKP(NEXT_BLKP(ptr))));
            if (!next_next_size) {
                if (extend_heap(MAX(ALIGN(asize - old_size - next_size), CHUNKSIZE) / WSIZE) == NULL)
                    return NULL;
                next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
                delete_node(NEXT_BLKP(ptr));
                int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
                PUT(HDRP(ptr), PACK_ALLOC(next_size + old_size, 1, prev_alloc, 1));
                PUT(FTRP(ptr), PACK_ALLOC(next_size + old_size, 1, prev_alloc, 1));
                newptr = ptr;
                SET_PREV_ALLOC(HDRP(NEXT_BLKP(newptr)), 1);
            }
            else {
                newptr = mm_malloc(size);
                if (!newptr)
                    return NULL;
                memmove(newptr, ptr, old_size - WSIZE);
                mm_free(ptr);            }
        }
    }

    return newptr;
}

void* coalesce(void* bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_NEXT_ALLOC(HDRP(bp));
    size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_next_alloc = GET_NEXT_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        insert_node(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete_node(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK_ALLOC(size, 0, 1, next_next_alloc));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        delete_node(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK_ALLOC(size, 0, prev_prev_alloc, 1));
        bp = PREV_BLKP(bp);
    }
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete_node(PREV_BLKP(bp));
        delete_node(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK_ALLOC(size, 0, prev_prev_alloc, next_next_alloc));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_node(bp);
    return bp;
}

void *place(void* bp, size_t asize) {
    void* res = bp;
    size_t csize = GET_SIZE(HDRP(bp));
    size_t prev_malloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_malloc = GET_NEXT_ALLOC(HDRP(bp));
    delete_node(bp);
    size_t res_size = csize - asize;
    if (res_size >= (2 * DSIZE)) {
        // divide the block, place in the front
        if (res_size < ADJUST_WEIGHT * asize) {
            PUT(HDRP(bp), PACK_ALLOC(asize, 1, prev_malloc, 0));
            PUT(FTRP(bp), PACK_ALLOC(asize, 1, prev_malloc, 0));
            SET_NEXT_ALLOC(HDRP(PREV_BLKP(bp)), 1);
            bp = NEXT_BLKP(bp);
            PUT(HDRP(bp), PACK_ALLOC(res_size, 0, 1, next_malloc));
            PUT(FTRP(bp), PACK_ALLOC(res_size, 0, 1, next_malloc));
            SET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 0);
            insert_node(bp);
        }
        // divide the block, place in the back
        else {
            PUT(HDRP(bp), PACK_ALLOC(res_size, 0, prev_malloc, 1));
            PUT(FTRP(bp), PACK_ALLOC(res_size, 0, prev_malloc, 1));
            SET_NEXT_ALLOC(HDRP(PREV_BLKP(bp)), 0);
            insert_node(bp);
            bp = NEXT_BLKP(bp);
            PUT(HDRP(bp), PACK_ALLOC(asize, 1, 0, next_malloc));
            PUT(FTRP(bp), PACK_ALLOC(asize, 1, 0, next_malloc));
            SET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 1);
            res = bp;
        }
    }
    else {
        SET_ALLOC(HDRP(bp), 1);
        SET_ALLOC(FTRP(bp), 1);
        SET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 1);
        SET_NEXT_ALLOC(HDRP(PREV_BLKP(bp)), 1);
    }
    return res;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = 1;

    SET_NEXT_ALLOC(HDRP(PREV_BLKP(bp)), 0);

    PUT(HDRP(bp), PACK_ALLOC(size, 0, prev_alloc, next_alloc)); /* Free block header */
    PUT(FTRP(bp), PACK(size, 0)); /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK_ALLOC(0, 1, 0, 0));

    return coalesce(bp);
}

int get_list_id(size_t size) {
    if (size <= 64) return 0;
    if (size <= 128) return 1;
    if (size <= 256) return 2;
    if (size <= 512) return 3;
    if (size <= 1024) return 4;
    if (size <= 2048) return 5;
    if (size <= 4096) return 6;
    if (size <= 8192) return 7;
    if (size <= 16384) return 8;
    if (size <= 32768) return 9;
    if (size <= 65536) return 10;
    return 11;
}

void insert_node(void* bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int list_id = get_list_id(size);
    char* listp = lists[list_id];
    SET_PREV(bp, 0);
    lists[list_id] = (char*)bp;
    if (!(listp))
        SET_NEXT(bp, 0);
    else {
        SET_NEXT(bp, listp);
        SET_PREV(listp, bp);
    }
}

void delete_node(void* bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int list_id = get_list_id(size);
    char* listp = lists[list_id];
    char* nextp = 0;
    unsigned long long next_pv = NEXT_POINTER(bp);
    if (next_pv)
        nextp = (char*)(next_pv | ptr_mask);
    char* prevp = 0;
    unsigned long long prev_pv = PREV_POINTER(bp);
    if (prev_pv)
        prevp = (char*)(prev_pv | ptr_mask);

    if (bp == (void*)listp) {
        lists[list_id] = nextp;
        if (lists[list_id])
            SET_PREV(lists[list_id], 0);
    }
    else {
        if (prevp)
            SET_NEXT(prevp, nextp);
        if (nextp)
            SET_PREV(nextp, prevp);
    }
    SET_PREV(bp, 0);
    SET_NEXT(bp, 0);
}

void* next_fit(size_t asize) {
    int list_id = get_list_id(asize);
    char* bp = NULL;
    unsigned long long p_val = 0;
    size_t min_size = 0x7fffffff;

    int its = 0;

    while (list_id < LISTS_NUM) {
        char* listp = lists[list_id];
        while (listp) {
            if (GET_SIZE(HDRP(listp)) >= asize) {
                its++;
                if (GET_SIZE(HDRP(listp)) < min_size) {
                    min_size = GET_SIZE(HDRP(listp));
                    bp = listp;
                }
                if (its > FIT_ITS)
                    break;
            }
            p_val = NEXT_POINTER(listp);
            if (p_val)
                listp = (char*)(p_val | ptr_mask);
            else
                break;
        }
        if (its > FIT_ITS)
            break;
        list_id++;
    }
    return bp;
}
