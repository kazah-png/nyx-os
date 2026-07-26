#include "slab.h"

#define SLAB_CACHE_SIZES {32, 64, 128, 256, 512, 1024}

static slab_cache_t slab_caches[SLAB_CACHE_MAX];
static int slab_cache_count = 0;

void slab_init(void) {
    uint32_t sizes[] = SLAB_CACHE_SIZES;
    for (int i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
        slab_caches[i].obj_size = sizes[i];
        slab_caches[i].pages = NULL;
        slab_cache_count++;
    }
}

static slab_cache_t* slab_find_cache(uint32_t size) {
    for (int i = 0; i < slab_cache_count; i++) {
        if (slab_caches[i].obj_size >= size)
            return &slab_caches[i];
    }
    return NULL;
}

static slab_page_t* slab_new_page(slab_cache_t* cache) {
    uint8_t* mem = (uint8_t*)alloc_page();
    if (!mem) return NULL;

    slab_page_t* page = (slab_page_t*)mem;
    uint32_t obj_size = cache->obj_size;
    uint32_t header_size = sizeof(slab_page_t);
    uint32_t avail = SLAB_PAGE_SIZE - header_size;
    uint32_t obj_count = avail / obj_size;

    page->obj_size = obj_size;
    page->total_objs = obj_count;
    page->free_objs = obj_count;
    page->free_list = NULL;

    uint8_t* obj_start = mem + header_size;
    for (uint32_t i = 0; i < obj_count; i++) {
        slab_slot_t* slot = (slab_slot_t*)(obj_start + i * obj_size);
        slot->next = page->free_list;
        page->free_list = slot;
    }

    page->next = cache->pages;
    page->prev = NULL;
    if (cache->pages) cache->pages->prev = page;
    cache->pages = page;

    return page;
}

void* slab_alloc(uint32_t size) {
    if (size == 0 || size > SLAB_MAX_OBJ) return NULL;

    slab_cache_t* cache = slab_find_cache(size);
    if (!cache) return NULL;

    for (slab_page_t* page = cache->pages; page; page = page->next) {
        if (page->free_list) {
            slab_slot_t* slot = page->free_list;
            // A free-list next pointer must be NULL or another slot in THIS page. A
            // wild write (the open "-1 writer" under extreme fork/execve churn) smashes
            // it to 0xFFFF..FFFF, which the old code dereferenced blindly -> opaque #PF
            // in slab_alloc. Catch it here with the corrupt slot/value instead.
            uint64_t pb = (uint64_t)page, nx = (uint64_t)slot->next;
            if (nx != 0 && (nx < pb + sizeof(slab_page_t) || nx >= pb + SLAB_PAGE_SIZE)) {
                printf("\n[SLABCORRUPT] obj_size=%u page=%p slot=%p next=0x%lx (outside page)\n",
                       cache->obj_size, (void*)page, (void*)slot, nx);
                kernel_panic("slab free-list corruption: slot %p next=0x%lx", (void*)slot, nx);
            }
            page->free_list = slot->next;
            page->free_objs--;
            return (void*)slot;
        }
    }

    slab_page_t* new_page = slab_new_page(cache);
    if (!new_page) return NULL;

    slab_slot_t* slot = new_page->free_list;
    new_page->free_list = slot->next;
    new_page->free_objs--;
    return (void*)slot;
}

void slab_free(void* ptr, uint32_t size) {
    if (!ptr || size == 0 || size > SLAB_MAX_OBJ) return;

    slab_cache_t* cache = slab_find_cache(size);
    if (!cache) return;

    uint64_t addr = (uint64_t)ptr;
    uint64_t page_base = addr & ~(SLAB_PAGE_SIZE - 1);

    slab_page_t* page = (slab_page_t*)page_base;
    slab_slot_t* slot = (slab_slot_t*)ptr;
    slot->next = page->free_list;
    page->free_list = slot;
    page->free_objs++;
}
