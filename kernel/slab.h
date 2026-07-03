#ifndef SLAB_H
#define SLAB_H

#include "kernel.h"

#define SLAB_PAGE_SIZE 4096
#define SLAB_MIN_OBJ   32
#define SLAB_MAX_OBJ   1024
#define SLAB_CACHE_MAX 6

typedef struct slab_slot {
    struct slab_slot* next;
} __attribute__((packed)) slab_slot_t;

typedef struct slab_page {
    struct slab_page* next;
    struct slab_page* prev;
    slab_slot_t* free_list;
    uint32_t obj_size;
    uint32_t total_objs;
    uint32_t free_objs;
} slab_page_t;

typedef struct {
    uint32_t obj_size;
    slab_page_t* pages;
} slab_cache_t;

void slab_init(void);
void* slab_alloc(uint32_t size);
void slab_free(void* ptr, uint32_t size);

#endif
