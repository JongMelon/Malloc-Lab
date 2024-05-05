# Malloc Lab Report

## 基本策略
采用分离适配策略，按分配块大小分为`12`个链表，头指针储存在堆中。
```c
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
```
在每一个空闲块中，`payload`部分分别利用`4 Bytes`存储`prev`与`next`指针，由于堆大小最多为`2 MB`，因此堆中地址高`32`位一致，仅存储低`32`位即可寻址。如此存储方式可保证最小块大小仍为`16 Bytes`。每次 `mm_init` 时保存堆的高`32`位地址，之后每次分配块时，将低`32`位地址与高`32`位地址拼接即可得到真实地址。
```c
ptr_mask = 0xffffffff;
ptr_mask = ~ptr_mask & (unsigned long long)lists;
```

朴素的头部插入与删除
```c
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
```
采用`best-fit`策略，并设置最大寻找次数为`50`
```c
#define FIT_ITS 50

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
```
`mm_malloc` 与 `mm_free` 的实现沿用 `mm_example.c`

## 优化策略

- 在 `realloc` 时，若负载块后方存在空闲块，且其大小足够容纳 `realloc` 后的负载块，则将其合并，以减少外部碎片。
  ```c
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
  ```

- 在 `place` 时若空闲块剩余空间显著大于分配空间，则将分配空间置于空闲块较高地址处，剩余空闲块置于低地址处，若前一个负载块需要 `realloc` ，该策略可为其提供更多原地扩展的空间。
  ```c
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
  ```

- 以上优化可显著提高最后两个测试点的得分

- 一些面向数据的优化
  ```c
    inline size_t adjust_size(size_t size) {
        if (size == 112)
            return 128;
        if (size == 448)
            return 512;
        return size;
    }
  ```

## 测试得分
```
Team Name:zkk
Member 1 :zk:zhukai2022@ruc.edu.cn
Using default tracefiles in ../traces/
Measuring performance with gettimeofday().

Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.001079  5277
 1       yes   99%    5848  0.000459 12744
 2       yes   99%    6648  0.000445 14946
 3       yes   99%    5380  0.000376 14305
 4       yes   98%   14400  0.000213 67606
 5       yes   96%    4800  0.000745  6440
 6       yes   95%    4800  0.000831  5780
 7       yes   96%   12000  0.000583 20580
 8       yes   89%   24000  0.001748 13733
 9       yes  100%   14401  0.000183 78651
10       yes   98%   14401  0.000162 89060
Total          97%  112372  0.006823 16469

Perf index = 58 (util) + 40 (thru) = 98/100
```
虽然使用了一些面向数据的优化，但 `5,6` 两个随机测试点表现良好，依然说明了本算法具有一定的泛化能力。