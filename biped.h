/**
* Copyright (c) 2022-2026 dustpg   mailto:dustpg@gmail.com
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use,
* copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following
* conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef BIPED_HEADER_H
#define BIPED_HEADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

//#define biped_malloc malloc
//#define biped_free free
//#define biped_realloc realloc
//#define biped_hash biped_hash_impl

typedef uint16_t biped_index_t;
typedef uint16_t biped_unit_t;
typedef uint32_t biped_product_t;

#ifdef __cplusplus
#define biped_noexcept noexcept
#else
#define biped_noexcept
#endif

struct biped_cache_ctx_impl;
typedef struct biped_cache_ctx_impl* biped_cache_ctx_t;

typedef struct {
    biped_unit_t x, y;
} biped_point2d_t;

typedef struct {
    biped_unit_t w, h;
} biped_size2d_t;

typedef struct {
    void* data; size_t size;
} biped_shared_combine_buffer_t;

/**
 * Block information filled by biped_cache_lock_key / biped_cache_lock_key_value.
 * @warning info->value may become invalid after the next call to *lock_key_value* (insert)
 */
typedef struct {
    uint32_t* value; // Pointer to value region (immediately after key). Valid only until next cache function call.
    biped_point2d_t position;
    biped_size2d_t aligned_size;
    biped_size2d_t real_size;
    biped_index_t id;
} biped_block_info_t;

typedef enum {
    biped_result_continue = -3,
    biped_result_out_of_cache = -2,
    biped_result_out_of_memory = -1,
    biped_result_valid = 0,
    biped_result_valid_but_high_pressure = 1, // locked area > 75%
} biped_result_t;

static inline bool
biped_is_success(biped_result_t result) biped_noexcept {
    return result >= biped_result_valid;
}

/**
 * @brief Create a cache context
 * @note Key and value sizes are specified in bytes, but 4-byte aligned.
 * @param side_len Side length of the cache
 * @param kv_len_in_byte Total key+value length in bytes
 * @param key_len_in_byte Key length in bytes
 * @return Cache context handle, or NULL on failure
 */
biped_cache_ctx_t
biped_cache_create(uint32_t side_len, uint32_t kv_len_in_byte, uint32_t key_len_in_byte) biped_noexcept;

/**
 * @brief Dispose a cache context
 * @param ctx Cache context handle to dispose
 */
void
biped_cache_dispose(biped_cache_ctx_t ctx) biped_noexcept;

/**
 * @brief Increase lock counter for item with specified key
 * @param ctx Cache context handle
 * @param key Key array to search for, cannot be NULL
 * @param[out] info Block information output (if found), cannot be NULL
 * @return biped_result_continue if not found, biped_result_valid if found
 * @note info->value is valid only until the next call to *lock_key_value*
 */
biped_result_t
biped_cache_lock_key(biped_cache_ctx_t ctx, const uint32_t key[], biped_block_info_t* info) biped_noexcept;

/**
 * @brief Insert and increase lock counter for new item
 * @note The item must not exist before calling this function
 * @param ctx Cache context handle
 * @param size Size of the item
 * @param key_value Key-value array for the new item, cannot be NULL
 * @param[out] info Block information output, cannot be NULL
 * @return Operation result
 * @note info->value is valid only until the next call to *lock_key_value*
 */
biped_result_t
biped_cache_lock_key_value(biped_cache_ctx_t ctx, biped_size2d_t size, const uint32_t key_value[], biped_block_info_t* info) biped_noexcept;

/**
 * @brief Decrease lock counter for items with specified IDs
 * @param ctx Cache context handle
 * @param IDs Array of block IDs to unlock, cannot be NULL if length > 0
 * @param length Number of IDs in the array
 */
void
biped_cache_unlock_id(biped_cache_ctx_t ctx, const biped_index_t IDs[], size_t length) biped_noexcept;

/**
 * @brief Clear all lock counter items
 * @param ctx Cache context handle
 */
void
biped_cache_force_unlock(biped_cache_ctx_t ctx) biped_noexcept;


#ifdef __cplusplus
}
#endif

#endif

#ifdef BIPED_C_IMPLEMENTATION

#ifndef biped_malloc
#define biped_malloc malloc
#endif
#ifndef biped_free
#define biped_free free
#endif
#ifndef biped_realloc
#define biped_realloc realloc
#endif
#ifndef biped_hash
#define biped_hash biped_hash_impl
#endif
#ifndef biped_min_w
#define biped_min_w (biped_unit_t)1
#endif
#ifndef biped_min_h
#define biped_min_h (biped_unit_t)2
#endif

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#ifdef _MSC_VER
#include <immintrin.h>
#endif

#ifndef biped_min
#define biped_min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef biped_max
#define biped_max(a, b) ((a) > (b) ? (a) : (b))
#endif

// ----------------------------------------------------------------------------
//                                  helper
// ----------------------------------------------------------------------------

#ifdef __cplusplus
#define biped_alignas(x) alignas(x)
#else
#define biped_alignas(x) _Alignas(x)
#endif

// Count leading zeros
static inline uint32_t
biped_leading_zeros_u32(uint32_t v) biped_noexcept {
#ifdef _MSC_VER
    return _lzcnt_u32(v);
#else
    return v ? __builtin_clz(v) : 32;
#endif
}

// Log2 roundup
static inline uint32_t
biped_log2_roundup_u32(uint32_t v) biped_noexcept {
    // cannot be zero
    return 32 - biped_leading_zeros_u32(v - 1);
}

// zero or power of 2
static inline bool biped_zero_or_pow2(uint32_t n) biped_noexcept { return (n & (n - 1)) == 0; }


static inline uint32_t
biped_to_level2(uint32_t size_w, uint32_t size_h) biped_noexcept {
    //assert(size.w && size.h && size.w < 0x8000 && size.h < 0x8000);
    uint32_t w = biped_log2_roundup_u32(size_w);
    uint32_t h = biped_log2_roundup_u32(size_h);
    uint32_t h1 = h - 1;
    uint32_t low = h > w ? 1 : 0;
    uint32_t high = w > h1 ? w : h1;
    return (high << 1) | low;
}
// Helper: convert size to level
static inline uint32_t
biped_to_level(biped_size2d_t size) biped_noexcept {
    return biped_to_level2(size.w, size.h);
}

// Helper: convert level to size
static inline biped_size2d_t
biped_to_size(uint16_t level) biped_noexcept {
    biped_size2d_t size;
    size.w = (uint16_t)(1 << (level >> 1));
    size.h = (uint16_t)(size.w << (level & 1));
    return size;
}

// Helper: convert level to area
static inline biped_product_t
biped_to_area(uint16_t level) biped_noexcept {
    return (biped_product_t)1 << (biped_product_t)level;
}

// Helper: check if cache is under high pressure (busy_area >= 75% of total_area)
static inline bool
biped_is_high_pressure(biped_product_t busy_area, biped_product_t total_area) biped_noexcept {
    return busy_area * 4 >= total_area * 3;
}

// ----------------------------------------------------------------------------
//                                  type struct
// ----------------------------------------------------------------------------


enum {
    biped_max_level = sizeof(uint32_t) * 8 - 2,
    biped_max_node_count = 0x7fff,
    biped_node_null = 0xffff,
    //shared_dummy_head = 0,
    //shared_dummy_tail = 1,
    biped_array_init_capacity = 128
};

#define BIPED_MAX_TIMESTAMP INT32_MAX

typedef struct {
    char* data;
    uint32_t unit_in_byte; // count in byte
    uint32_t key_length; // length in uint32_t
#ifndef NDEBUG
    uint32_t dlength;
#endif
} biped_node_ctx;

typedef struct {
    biped_index_t prev, next;
} biped_index_node_t;

typedef struct linked_list {
    biped_index_t head, tail;
} biped_linked_list_t;


typedef struct {
    biped_index_node_t base;
    biped_index_node_t klass;
    uint8_t flag; // in hashmap
    uint8_t level;
    uint16_t counter; // lock
    biped_point2d_t pos;
    biped_size2d_t real;
    int32_t time;
    // FAM: c++ compatibility
    uint32_t key_value[1];
} biped_node_t;

static void
biped_detach_node_klass(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept;
static void
biped_detach_node_base(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept;
static void
biped_push_back_klass(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept;
static void
biped_push_front_klass(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept;
static void
biped_push_back(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept;
static void
biped_push_front(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept;

typedef struct {
    biped_alignas(uint32_t) biped_index_t tag; // high-(16)bit hash value
    biped_index_t index;
} biped_hash_slot_t;


// 2power based open addressing hash table [load factor < 0.5]
typedef struct {
    biped_hash_slot_t* lookup_table;
    uint32_t slot_count; // 2-power
    uint32_t item_count; // load 
} biped_openaddr_hash_t;


typedef struct {
    char* data;
    uint32_t length; // length of items
    uint32_t capacity; // capacity of items
} biped_array_t;

static void
biped_array_init(biped_array_t*) biped_noexcept;
static void
biped_array_uninit(biped_array_t*) biped_noexcept;
static bool
biped_array_resize(biped_array_t*, size_t unit, uint32_t size) biped_noexcept;
static bool
biped_array_push_index(biped_array_t*, biped_index_t) biped_noexcept;
static bool
biped_alloc_nodes(biped_cache_ctx_t ctx, uint32_t count, biped_index_t output[biped_max_level]) biped_noexcept;


typedef struct {
    uint32_t slot;
    biped_index_t index;
} biped_hashmap_search_result;

static bool
biped_hashmap_init(biped_openaddr_hash_t*, uint32_t init_slot_count) biped_noexcept;
static void
biped_hashmap_uninit(biped_openaddr_hash_t*) biped_noexcept;
static bool
biped_hashmap_rehash(biped_openaddr_hash_t*, biped_node_ctx ctx) biped_noexcept;
static void
biped_hashmap_insert(biped_openaddr_hash_t*, biped_index_t, uint32_t hashcode) biped_noexcept;
static void
biped_hashmap_remove(biped_openaddr_hash_t*, uint32_t slot_index, biped_node_ctx ctx) biped_noexcept;
static biped_hashmap_search_result
biped_hashmap_search(const biped_openaddr_hash_t*, const uint32_t key[], biped_node_ctx ctx) biped_noexcept;
static bool
biped_hashmap_try_rehash(biped_openaddr_hash_t*, biped_node_ctx ctx) biped_noexcept;
static bool
biped_hashmap_try_remove(biped_openaddr_hash_t*, biped_node_t* node, biped_index_t, biped_node_ctx ctx) biped_noexcept;

struct biped_cache_ctx_impl {
    biped_openaddr_hash_t hashmap;
    biped_array_t biped_index_pool;
    biped_array_t biped_node_pool;

    biped_shared_combine_buffer_t* shared_buffer;
    biped_shared_combine_buffer_t standalone_buffer;

    uint16_t max_Level;
    uint16_t min_level;

    uint32_t size_of_node;
    uint32_t length_of_key; // in uint32_t
    uint32_t size_of_key_value; // in uint32_t
    int32_t timestamp_warm;
    int32_t timestamp_cold;

    biped_product_t total_area;  // total area (side_len * side_len)
    biped_product_t busy_area;   // busy list area

    biped_linked_list_t busy_list;
    // 0: 1x1 is a bad size, for all free list now
    biped_linked_list_t free_lists[1 + biped_max_level];

};

static biped_result_t
biped_behavior_attempt(biped_cache_ctx_t, uint16_t level) biped_noexcept;

static biped_result_t
biped_behavior_browse(biped_cache_ctx_t, uint16_t level) biped_noexcept;

static biped_result_t
biped_behavior_combine(biped_cache_ctx_t, uint16_t level) biped_noexcept;

static biped_result_t
biped_op_downsplit(biped_cache_ctx_t, biped_index_t index, uint16_t from, uint16_t to) biped_noexcept;

static inline void
biped_update_cold_timestamp(biped_cache_ctx_t ctx, biped_node_ctx node_ctx) biped_noexcept;

static inline biped_node_ctx
biped_get_node_ctx(biped_cache_ctx_t ctx) biped_noexcept {
    biped_node_ctx rv;
    rv.data = ctx->biped_node_pool.data;
    rv.unit_in_byte = ctx->size_of_node;
    rv.key_length = ctx->length_of_key;
#ifndef NDEBUG
    assert(rv.data && rv.unit_in_byte && rv.key_length);
    rv.dlength = ctx->biped_node_pool.length;
#endif
    return rv;
}

static inline biped_node_t* biped_get_node(biped_node_ctx ctx, biped_index_t index) biped_noexcept {
    const size_t offset = (size_t)ctx.unit_in_byte * (size_t)index;
    assert(index < ctx.dlength);
    return (biped_node_t*)(ctx.data + offset);
}

static inline void
biped_update_cold_timestamp(biped_cache_ctx_t ctx, biped_node_ctx node_ctx) biped_noexcept {
    const biped_index_t index = ctx->free_lists[0].tail;
    if (index == biped_node_null) {
        ctx->timestamp_cold = ctx->timestamp_warm;
    }
    else {
        const biped_node_t* node = biped_get_node(node_ctx, index);
        ctx->timestamp_cold = node->time;
    }
}

typedef struct { int32_t value; biped_index_t index; uint16_t level; } biped_coldest_t;

static inline bool biped_is_cold_enough(biped_cache_ctx_t ctx, biped_coldest_t result) biped_noexcept {
    const int32_t value = ctx->timestamp_warm - ctx->timestamp_cold;
    return result.value + 1 >= value / 2;
}
static inline bool biped_is_cold_enough_strict(biped_cache_ctx_t ctx, biped_coldest_t result) biped_noexcept {
    const int32_t value = ctx->timestamp_warm - ctx->timestamp_cold;
    return result.value > value / 4;
}

static inline biped_coldest_t
biped_find_level(biped_cache_ctx_t ctx, uint16_t level) biped_noexcept {
    assert(ctx && level);
    uint16_t max_level = level;
    int max_value = -1;
    uint16_t max_index = ctx->free_lists[level].tail;
    const int32_t now = ctx->timestamp_warm;
    if (max_index != biped_node_null) {
        const biped_node_t* node = biped_get_node(biped_get_node_ctx(ctx), max_index);
        max_value = now - node->time;
    }
    biped_coldest_t rv = { max_value, max_index, max_level };
    return rv;
}

static inline biped_coldest_t
biped_find_coldest(biped_cache_ctx_t ctx, uint16_t level) biped_noexcept {
    const uint16_t max_Level_i = ctx->max_Level + 1;
    const int32_t now = ctx->timestamp_warm;
    uint16_t max_level = 0;
    int max_value = -1;
    uint16_t max_index = biped_node_null;
    biped_node_ctx node_ctx = biped_get_node_ctx(ctx);
    for (; level < max_Level_i; ++level) {
        const biped_index_t obj = ctx->free_lists[level].tail;
        if (obj != biped_node_null) {
            biped_node_t* node = biped_get_node(node_ctx, obj);
            const int32_t value = node->flag ? now - node->time : BIPED_MAX_TIMESTAMP;
            if (value > max_value) {
                max_value = value;
                max_index = obj;
                max_level = level;
            }
        }
    }
    biped_coldest_t rv = { max_value, max_index, max_level };
    return rv;
}

// BIPED TO INDEX
static inline size_t
biped_to_index(const biped_node_t* node, biped_unit_t xshift, biped_unit_t yshift, biped_unit_t diff) biped_noexcept {
    const biped_unit_t x = node->pos.x >> xshift;
    const biped_unit_t y = node->pos.y >> yshift;
    const biped_unit_t wpower = (biped_unit_t)(diff + 1) >> 1;
#ifndef NDEBUG
    const biped_unit_t width = 1 << wpower;
    //const biped_size2d_t block_size = biped_to_size(lv);
    assert(((size_t)((size_t)(y) << wpower) | (size_t)x) == ((size_t)y * (size_t)width + (size_t)x));
#endif
    const size_t index_in_page = ((size_t)y << wpower) | x;
    return index_in_page;
};

// INDEX TO BIPED
static inline void
biped_to_biped(size_t index, biped_unit_t diff, biped_unit_t xshift, biped_unit_t yshift, biped_node_t* biped) biped_noexcept {
    const biped_unit_t wpower = (biped_unit_t)(diff + 1) >> 1;
    const biped_unit_t y = (biped_unit_t)(index >> (size_t)(wpower));
    const biped_unit_t x = index & ((1 << wpower) - 1);
    biped->pos.x = x << xshift;
    biped->pos.y = y << yshift;
};

static inline void
biped_recycle_node(biped_cache_ctx_t ctx, biped_node_t* node, biped_index_t index) biped_noexcept {
#ifndef NDEBUG
    memset(node, 0xdd, sizeof(biped_node_t));
#endif
    // it's accepted if out of memory here
    biped_array_push_index(&ctx->biped_index_pool, index);
}

uint32_t biped_hash_impl(const uint32_t[], uint32_t count) biped_noexcept;


// ----------------------------------------------------------------------------
//                               cache2d
// ----------------------------------------------------------------------------

biped_cache_ctx_t
biped_cache_create(uint32_t side_len, uint32_t kv_len_in_byte, uint32_t key_len_in_byte) biped_noexcept {
    assert(kv_len_in_byte >= key_len_in_byte);
    assert(side_len >= 16 && "TOO SMALL");
    assert(biped_zero_or_pow2(side_len) && "POW OF 2");

    if (side_len > 0x8000)
        return NULL;

    biped_cache_ctx_t ctx = (biped_cache_ctx_t)biped_malloc(sizeof(struct biped_cache_ctx_impl));
    if (ctx) {
        memset(ctx, 0, sizeof(struct biped_cache_ctx_impl));

        // INIT HASH MAP
        if (!biped_hashmap_init(&ctx->hashmap, biped_array_init_capacity)) {
            goto error_path;
        }

        // INIT ARRAY
        biped_array_init(&ctx->biped_index_pool);
        biped_array_init(&ctx->biped_node_pool);

        // INIT SHARED BUFFERS
        ctx->standalone_buffer.data = NULL;
        ctx->standalone_buffer.size = 0;
        ctx->shared_buffer = &ctx->standalone_buffer;

        // INIT LEVEL AND TIMESTAMP
        const uint16_t lv = (uint16_t)biped_to_level2(side_len, side_len);
        ctx->min_level = ctx->max_Level = lv;
        ctx->timestamp_warm = 0;
        ctx->timestamp_cold = 0;

        // INIT NODE SIZE AND KEY LENGTH
        const uint32_t key_len_in_u32 = (key_len_in_byte + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        const uint32_t kv_len_in_u32 = (kv_len_in_byte + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        ctx->length_of_key = key_len_in_u32;
        ctx->size_of_key_value = kv_len_in_u32 * sizeof(uint32_t);
        // Calculate node size: base structure + all_len * sizeof(uint32_t)
        // key_value[1] is FAM placeholder, so we use all_len directly
        ctx->size_of_node = offsetof(biped_node_t, key_value) + kv_len_in_u32 * sizeof(uint32_t);

        // INIT LINKED LISTS
        ctx->busy_list.head = biped_node_null;
        ctx->busy_list.tail = biped_node_null;
        for (uint32_t i = 0; i <= biped_max_level; ++i) {
            ctx->free_lists[i].head = biped_node_null;
            ctx->free_lists[i].tail = biped_node_null;
        }

        // INIT AREA
        ctx->total_area = (biped_product_t)side_len * (biped_product_t)side_len;
        ctx->busy_area = 0;

        // TOP LEVEL
        if (!biped_array_resize(&ctx->biped_node_pool, ctx->size_of_node, 1)) {
            goto error_path;
        }

        const biped_index_t first = 0;

        biped_node_t* const node = biped_get_node(biped_get_node_ctx(ctx), first);
        // ctx->size_of_node, but offsetof(biped_node_t, key_value) is enough
        memset(node, 0, offsetof(biped_node_t, key_value));

        node->real.w = (biped_unit_t)side_len;
        node->real.h = (biped_unit_t)side_len;
        node->base.prev = biped_node_null;
        node->base.next = biped_node_null;
        node->klass.prev = biped_node_null;
        node->klass.next = biped_node_null;
        node->level = (uint8_t)lv;

        ctx->free_lists[0].head = first;
        ctx->free_lists[0].tail = first;
        ctx->free_lists[lv].head = first;
        ctx->free_lists[lv].tail = first;

        return ctx;
    }
error_path:
    biped_cache_dispose(ctx);
    return NULL;
}

void
biped_cache_dispose(biped_cache_ctx_t ctx) biped_noexcept {
    if (!ctx)
        return;

    // UNINIT HASH MAP
    biped_hashmap_uninit(&ctx->hashmap);

    // UNINIT ARRAYS
    biped_array_uninit(&ctx->biped_index_pool);
    biped_array_uninit(&ctx->biped_node_pool);

    // UNINIT BUFFERS
    if (ctx->standalone_buffer.data) {
        biped_free(ctx->standalone_buffer.data);
        ctx->standalone_buffer.data = NULL;
        ctx->standalone_buffer.size = 0;
    }

    // FREE CONTEXT
    biped_free(ctx);
}


static void biped_split_node(biped_node_t* main, biped_node_t* sub) biped_noexcept {
    assert(main && sub);
    assert(main->level > 1 && "cannot split into level 0");
    main->base.prev = biped_node_null;
    main->base.next = biped_node_null;
    main->klass.prev = biped_node_null;
    main->klass.next = biped_node_null;
    main->level--;
    main->real = biped_to_size(main->level);
    *sub = *main;

    biped_unit_t offset = (biped_unit_t)(1 << (main->level >> 1));
    biped_unit_t ymask = (biped_unit_t)(main->level & 1) - 1;
    biped_unit_t xmask = ~ymask;

    sub->pos.x += xmask & offset;
    sub->pos.y += ymask & offset;
}


static biped_result_t
biped_op_downsplit(biped_cache_ctx_t ctx, biped_index_t index, uint16_t from, uint16_t to) biped_noexcept {
    assert(from >= to && from && to && from <= biped_max_level);
    const uint16_t count = from - to;
    biped_index_t output[biped_max_level];
    if (!biped_alloc_nodes(ctx, count, output))
        return biped_result_out_of_memory;

    const biped_node_ctx node_ctx = biped_get_node_ctx(ctx);

    biped_node_t* const item = biped_get_node(node_ctx, index);

    biped_hashmap_try_remove(&ctx->hashmap, item, index, node_ctx);

    item->time = ctx->timestamp_cold;
    biped_detach_node_base(ctx->free_lists + 0, item, index, node_ctx);
    biped_detach_node_klass(ctx->free_lists + from, item, index, node_ctx);

    uint16_t level = from;
    for (uint16_t i = 0; i != count; ++i) {
        const biped_index_t subi = output[i];
        biped_node_t* const sub = biped_get_node(node_ctx, subi);
        biped_split_node(item, sub);
        biped_push_back(ctx->free_lists + 0, sub, subi, node_ctx);
        --level;
        biped_push_back_klass(ctx->free_lists + level, sub, subi, node_ctx);
    }

    assert(item->counter == 0);
    item->counter = 1;
    biped_push_back(&ctx->busy_list, item, index, node_ctx);
    // update busy area
    ctx->busy_area += biped_to_area(item->level);
    biped_update_cold_timestamp(ctx, node_ctx);
    return biped_result_valid;
}


biped_result_t
biped_cache_lock_key(biped_cache_ctx_t ctx, const uint32_t key[], biped_block_info_t* info) biped_noexcept {
    assert(info && key);
    if (!ctx)
        return biped_result_continue;

    const biped_node_ctx node_ctx = biped_get_node_ctx(ctx);
    const biped_hashmap_search_result result = biped_hashmap_search(&ctx->hashmap, key, node_ctx);

    if (result.index == biped_node_null) {
        return biped_result_continue;
    }

    const biped_index_t index = result.index;
    biped_node_t* const node = biped_get_node(node_ctx, index);

    // If node is not locked yet (counter == 0), move it from free_lists to busy_list
    if (node->counter == 0) {
        biped_detach_node_base(ctx->free_lists + 0, node, index, node_ctx);
        biped_detach_node_klass(ctx->free_lists + node->level, node, index, node_ctx);
        node->counter = 1;
        biped_push_back(&ctx->busy_list, node, index, node_ctx);
        // update busy area
        ctx->busy_area += biped_to_area(node->level);
        biped_update_cold_timestamp(ctx, node_ctx);
    }
    else {
        // Node is already locked, increment counter 
        // TODO: and move to end of busy_list ?
        node->counter++;
    }

    // Fill output info
    const uint32_t length_of_key = ctx->length_of_key;
    info->position = node->pos;
    info->aligned_size = biped_to_size(node->level);
    info->real_size = node->real;
    info->id = index;
    info->value = node->key_value + length_of_key;

    return biped_result_valid;
}

biped_result_t
biped_cache_lock_key_value(biped_cache_ctx_t ctx, biped_size2d_t size, const uint32_t key_value[], biped_block_info_t* info) biped_noexcept {
    assert(info && key_value);
    if (!ctx)
        return biped_result_continue;

    assert(biped_is_success(biped_cache_lock_key(ctx, key_value, info)) == false);

    biped_size2d_t clamped;
    clamped.w = biped_max(size.w, biped_min_w);
    clamped.h = biped_max(size.h, biped_min_h);
    const uint16_t l = (uint16_t)biped_to_level(clamped);
    if (l > ctx->max_Level)
        return biped_result_out_of_cache;

    // rehash if necessary
    // HINT: do not save biped_get_node_ctx to local variable here,
    //       because it may be changed in "behavior_browse"
    if (!biped_hashmap_try_rehash(&ctx->hashmap, biped_get_node_ctx(ctx))) {
        return biped_result_out_of_memory;
    }

    ctx->min_level = biped_min(ctx->min_level, l);

    biped_result_t result = biped_result_continue;

    // A
    if (result == biped_result_continue)
        result = biped_behavior_attempt(ctx, l);
    // B
    if (result == biped_result_continue)
        result = biped_behavior_browse(ctx, l);
    // C
    if (result == biped_result_continue)
        result = biped_behavior_combine(ctx, l);

    // OUTPUT
    if (biped_is_success(result)) {
        assert(ctx->busy_list.tail != biped_node_null);
        // write info
        const uint32_t length_of_key = ctx->length_of_key;
        const biped_index_t id = ctx->busy_list.tail;
        biped_node_t* const node = biped_get_node(biped_get_node_ctx(ctx), id);
        node->real = size;
        memcpy(node->key_value, key_value, ctx->size_of_key_value);
        // hashmap
        assert(node->flag == 0 && node->counter == 1);
        node->flag = 1;
        const uint32_t hashcode = biped_hash(key_value, length_of_key);
        biped_hashmap_insert(&ctx->hashmap, id, hashcode);
        // output
        info->position = node->pos;
        info->aligned_size = biped_to_size(node->level);
        info->real_size = size;
        info->id = id;
        info->value = node->key_value + length_of_key;

        // Check for high pressure
        if (biped_is_high_pressure(ctx->busy_area, ctx->total_area)) {
            result = biped_result_valid_but_high_pressure;
        }
    }
#ifndef NDEBUG
    else {
        memset(info, 0xcd, sizeof(*info));
    }
#endif
    return result;
}

void
biped_cache_unlock_id(biped_cache_ctx_t ctx, const biped_index_t IDs[], size_t length) biped_noexcept {
    if (!ctx)
        return;

    const biped_node_ctx node_ctx = biped_get_node_ctx(ctx);
    const biped_index_t node_count = (biped_index_t)ctx->biped_node_pool.length;

    for (size_t i = 0; i < length; i++) {
        const biped_index_t index = IDs[i];
        if (index >= node_count) {
            assert(!"index out of range");
            continue;
        }

        biped_node_t* const node = biped_get_node(node_ctx, index);
        if (node->counter == 0) {
            assert(!"cannot unlock node that is not locked");
            continue;
        }

        node->counter--;
        if (node->counter == 0) {
            const int32_t time = ++ctx->timestamp_warm;
            node->time = time;
            biped_detach_node_base(&ctx->busy_list, node, index, node_ctx);
            // update busy area
            ctx->busy_area -= biped_to_area(node->level);
            biped_push_front(ctx->free_lists + 0, node, index, node_ctx);
            biped_push_front_klass(ctx->free_lists + node->level, node, index, node_ctx);
        }
    }
}

void
biped_cache_force_unlock(biped_cache_ctx_t ctx) biped_noexcept {
    if (!ctx)
        return;

    const biped_node_ctx node_ctx = biped_get_node_ctx(ctx);
    biped_index_t node = ctx->busy_list.head;

    while (node != biped_node_null) {
        biped_node_t* obj = biped_get_node(node_ctx, node);
        const biped_index_t next = obj->base.next;

        obj->counter = 0;
        const int32_t time = ++ctx->timestamp_warm;
        obj->time = time;

        biped_detach_node_base(&ctx->busy_list, obj, node, node_ctx);
        // update busy area (use aligned size based on level, not real size)
        ctx->busy_area -= biped_to_area(obj->level);
        biped_push_front(ctx->free_lists + 0, obj, node, node_ctx);
        biped_push_front_klass(ctx->free_lists + obj->level, obj, node, node_ctx);

        node = next;
    }
}

static biped_result_t
biped_behavior_attempt(biped_cache_ctx_t ctx, uint16_t level) biped_noexcept {
    // ATTEMPT: O(1)
    // check last node is we wanted (LRU)
    const biped_index_t index = ctx->free_lists[0].tail;
    if (index != biped_node_null) {
        const biped_node_t* node = biped_get_node(biped_get_node_ctx(ctx), index);
        if (node->level == level) {
            return biped_op_downsplit(ctx, index, level, level);
        }
    }
    return biped_result_continue;
}


static biped_result_t
biped_behavior_browse(biped_cache_ctx_t ctx, uint16_t level) biped_noexcept {
    // BROWSE: O(logN)
    const biped_coldest_t same = biped_find_level(ctx, level);
    const biped_coldest_t diff = biped_find_coldest(ctx, level);
    biped_coldest_t coldest;
    if (diff.index == biped_node_null)
        return biped_result_continue;

    // EMPTY NODE 1ST
    if (diff.value == BIPED_MAX_TIMESTAMP)
        coldest = diff;
    // SAME LEVEL 2ND
    else if (biped_is_cold_enough_strict(ctx, same))
        coldest = same;
    // DIFF LEVEL 3RD
    else if (biped_is_cold_enough(ctx, same))
        coldest = diff;
    else
        return biped_result_continue;

    return biped_op_downsplit(ctx, coldest.index, coldest.level, level);
}

//static biped_result_t
//biped_combine_u8(biped_cache_ctx_t ctx, uint16_t diff, uint16_t lv) biped_noexcept;
static biped_result_t
biped_combine_u16(biped_cache_ctx_t ctx, uint16_t diff, uint16_t lv) biped_noexcept;
//static biped_result_t
//biped_combine_u32(biped_cache_ctx_t ctx, uint16_t diff, uint16_t lv) biped_noexcept;
static biped_result_t
biped_combine_final(biped_cache_ctx_t ctx, size_t last_index, biped_node_t* last_obj, uint16_t last, uint16_t lv, uint16_t diff) biped_noexcept;

static biped_result_t
biped_behavior_combine(biped_cache_ctx_t ctx, uint16_t level) biped_noexcept {
    // COMBINE: O(N)
    assert(level >= ctx->min_level);
    const uint16_t level_diff_top = (uint16_t)(ctx->max_Level - level);
    const uint16_t level_diff_bottom = (uint16_t)(level - ctx->min_level);
    if (level_diff_bottom < 16) {
        return biped_combine_u16(ctx, level_diff_top, level);
    }
    else {
        assert(!"ABORT");
        return biped_result_out_of_cache;
    }
}

static biped_result_t
biped_combine_u16(biped_cache_ctx_t ctx, uint16_t diff, uint16_t lv) biped_noexcept {
    const size_t count = (size_t)1 << (size_t)diff;
    const size_t bytes = (size_t)count * sizeof(uint16_t);
    if (ctx->shared_buffer->size < bytes) {
        void* new_data = biped_realloc(ctx->shared_buffer->data, bytes);
        if (!new_data) {
            return biped_result_out_of_memory;
        }
        ctx->shared_buffer->data = new_data;
        ctx->shared_buffer->size = bytes;
    }
    uint16_t* const data = (uint16_t*)ctx->shared_buffer->data;
    memset(data, 0, bytes);

    biped_index_t node = ctx->free_lists[0].tail;

    const biped_unit_t xshift = (biped_unit_t)(lv >> 1);
    const biped_unit_t yshift = (biped_unit_t)((lv + 1) >> 1);

    uint16_t last = biped_node_null;
    biped_node_t* last_obj = NULL;
    size_t last_index = 0;

    biped_node_ctx node_ctx = biped_get_node_ctx(ctx);

    while (node != biped_node_null) {
        biped_node_t* obj = biped_get_node(node_ctx, node);
        const biped_index_t next = obj->base.prev;

        if (obj->level >= lv) {
            // already
            last = node;
            last_obj = obj;
            break;
        }

        const size_t index = biped_to_index(obj, xshift, yshift, diff);
        assert(index < count);
        uint16_t* combine = &data[index];
        const uint16_t local_diff = lv - obj->level;
        const uint16_t mask = (uint16_t)1 << (uint16_t)(16 - local_diff);
        *combine += mask;
        if (*combine == 0) {
            assert(mask != 0);
            last = node;
            last_obj = obj;
            last_index = index;
            break;
        }
        node = next;
    }

    return biped_combine_final(ctx, last_index, last_obj, last, lv, diff);
}

//extern int g_biped_combine_final_counter;

static biped_result_t
biped_combine_final(biped_cache_ctx_t ctx, size_t last_index, biped_node_t* last_obj, uint16_t last, uint16_t lv, uint16_t diff) biped_noexcept {
    if (!last_obj)
        return biped_result_out_of_cache;

    // already
    if (last_obj->level >= lv) {
        return biped_op_downsplit(ctx, last, last_obj->level, lv);
    }

    const biped_unit_t xshift = (biped_unit_t)(lv >> 1);
    const biped_unit_t yshift = (biped_unit_t)((lv + 1) >> 1);


    biped_index_t node = ctx->free_lists[0].tail;
    biped_node_ctx node_ctx = biped_get_node_ctx(ctx);

    while (node != last) {
        biped_node_t* obj = biped_get_node(node_ctx, node);
        const biped_index_t next = obj->base.prev;
        const size_t index = biped_to_index(obj, xshift, yshift, diff);
        if (index == last_index) {
            biped_detach_node_base(ctx->free_lists + 0, obj, node, node_ctx);
            biped_detach_node_klass(ctx->free_lists + obj->level, obj, node, node_ctx);
            biped_hashmap_try_remove(&ctx->hashmap, obj, node, node_ctx);
            biped_recycle_node(ctx, obj, node);
        }
        node = next;
    }

    biped_detach_node_base(ctx->free_lists + 0, last_obj, last, node_ctx);
    biped_detach_node_klass(ctx->free_lists + last_obj->level, last_obj, last, node_ctx);
    biped_hashmap_try_remove(&ctx->hashmap, last_obj, last, node_ctx);

    biped_to_biped(last_index, diff, xshift, yshift, last_obj);
    last_obj->level = (uint8_t)lv;
    last_obj->real = biped_to_size(lv);  // update real size to match level
    last_obj->counter = 1;
    biped_push_back(&ctx->busy_list, last_obj, last, node_ctx);

    // update busy area
    ctx->busy_area += biped_to_area(last_obj->level);

    biped_update_cold_timestamp(ctx, node_ctx);
    //g_biped_combine_final_counter++;
    return biped_result_valid;
}

// ----------------------------------------------------------------------------
//                                  array
// ----------------------------------------------------------------------------

static void
biped_array_init(biped_array_t* arr) biped_noexcept {
    assert(arr);
    arr->data = NULL;
    arr->length = 0;
    arr->capacity = 0;
}

static void
biped_array_uninit(biped_array_t* arr) biped_noexcept {
    assert(arr);
    biped_free(arr->data);
    arr->data = NULL;
    arr->length = 0;
    arr->capacity = 0;
}

static bool
biped_array_resize(biped_array_t* arr, size_t unit, uint32_t size) biped_noexcept {
    assert(arr && unit > 0);

    // If current capacity is sufficient, just update length
    if (size <= arr->capacity) {
        arr->length = size;
        return true;
    }

    if (size >= (uint32_t)INT32_MAX)
        return false;


    uint32_t new_capacity = arr->capacity;
    if (new_capacity < 2) {
        new_capacity = biped_array_init_capacity;
    }

    // Grow by 1.5x until capacity >= size
    while (new_capacity < size) {
        const uint32_t half = (new_capacity >> 1);
        const uint32_t step = half ? half : 1;
        if (new_capacity > UINT32_MAX - step) {
            new_capacity = size;
            break;
        }
        new_capacity = new_capacity + step;
    }

    // Reallocate memory
    const size_t new_size = (size_t)new_capacity * unit;
    char* new_data = (char*)biped_realloc(arr->data, new_size);
    if (!new_data) {
        return false;
    }

    arr->data = new_data;
    arr->capacity = new_capacity;
    arr->length = size;
    return true;
}


static bool
biped_array_push_index(biped_array_t* arr, biped_index_t index) biped_noexcept {
    assert(arr);
    const uint32_t old = arr->length;
    if (biped_array_resize(arr, sizeof(biped_index_t), old + 1)) {
        ((biped_index_t*)arr->data)[old] = index;
        return true;
    }
    return false;
}

static bool
biped_alloc_nodes(biped_cache_ctx_t ctx, uint32_t count, biped_index_t output[biped_max_level]) biped_noexcept {
    assert(ctx && output);
    assert(count <= biped_max_level);

    biped_array_t* const pool = &ctx->biped_index_pool; // stack of free indices
    biped_array_t* const nodes = &ctx->biped_node_pool; // node storage

    const uint32_t pool_size = (pool->length < count) ? pool->length : count;
    const uint32_t ex = count - pool_size;

    const uint32_t index = nodes->length;
    if (ex) {
        if (index + ex > biped_max_node_count)
            return false;
        if (!biped_array_resize(nodes, ctx->size_of_node, index + ex))
            return false;
    }

    if (pool_size) {
        const biped_index_t* const pool_data = (const biped_index_t*)pool->data;
        const uint32_t local = pool->length - pool_size;
        memcpy(output, pool_data + local, (size_t)pool_size * sizeof(biped_index_t));
        pool->length = local;
    }

    for (uint32_t i = 0; i != ex; ++i) {
        output[pool_size + i] = (biped_index_t)(index + i);
    }

    return true;
}


// ----------------------------------------------------------------------------
//                          index based linked list
// ----------------------------------------------------------------------------


static void
biped_detach_node_klass(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept {
    assert(node == biped_get_node(node_ctx, index));

    if (list->head == index) {
        assert(node->klass.prev == biped_node_null && "head node should have null prev");
        list->head = node->klass.next;
    }
    else {
        biped_node_t* tmp = biped_get_node(node_ctx, node->klass.prev);
        tmp->klass.next = node->klass.next;
    }

    if (list->tail == index) {
        assert(node->klass.next == biped_node_null && "tail node should have null next");
        list->tail = node->klass.prev;
    }
    else {
        biped_node_t* tmp = biped_get_node(node_ctx, node->klass.next);
        tmp->klass.prev = node->klass.prev;
    }

    node->klass.prev = biped_node_null;
    node->klass.next = biped_node_null;
}

static void
biped_detach_node_base(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept {
    assert(node == biped_get_node(node_ctx, index));

    if (list->head == index) {
        assert(node->base.prev == biped_node_null && "head node should have null prev");
        list->head = node->base.next;
    }
    else {
        biped_node_t* tmp = biped_get_node(node_ctx, node->base.prev);
        tmp->base.next = node->base.next;
    }

    if (list->tail == index) {
        assert(node->base.next == biped_node_null && "tail node should have null next");
        list->tail = node->base.prev;
    }
    else {
        biped_node_t* tmp = biped_get_node(node_ctx, node->base.next);
        tmp->base.prev = node->base.prev;
    }

    node->base.prev = biped_node_null;
    node->base.next = biped_node_null;
}

static void
biped_push_back_klass(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept {
    assert(node == biped_get_node(node_ctx, index));
    // If list is empty, this node becomes both head and tail
    if (list->tail == biped_node_null) {
        assert(list->head == biped_node_null && "empty list should have null head");
        list->head = index;
        list->tail = index;
        node->klass.prev = biped_node_null;
        node->klass.next = biped_node_null;
    }
    else {
        // Link new node to current tail
        biped_node_t* tail_node = biped_get_node(node_ctx, list->tail);
        tail_node->klass.next = index;
        node->klass.prev = list->tail;
        node->klass.next = biped_node_null;
        // Update list tail to new node
        list->tail = index;
    }
}

static void
biped_push_front_klass(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept {
    assert(node == biped_get_node(node_ctx, index));
    // If list is empty, this node becomes both head and tail
    if (list->head == biped_node_null) {
        assert(list->tail == biped_node_null && "empty list should have null tail");
        list->head = index;
        list->tail = index;
        node->klass.prev = biped_node_null;
        node->klass.next = biped_node_null;
    }
    else {
        // Link new node to current head
        biped_node_t* head_node = biped_get_node(node_ctx, list->head);
        head_node->klass.prev = index;
        node->klass.prev = biped_node_null;
        node->klass.next = list->head;
        // Update list head to new node
        list->head = index;
    }
}

static void
biped_push_back(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept {
    assert(node == biped_get_node(node_ctx, index));
    // If list is empty, this node becomes both head and tail
    if (list->tail == biped_node_null) {
        assert(list->head == biped_node_null && "empty list should have null head");
        list->head = index;
        list->tail = index;
        node->base.prev = biped_node_null;
        node->base.next = biped_node_null;
    }
    else {
        // Link new node to current tail
        biped_node_t* tail_node = biped_get_node(node_ctx, list->tail);
        tail_node->base.next = index;
        node->base.prev = list->tail;
        node->base.next = biped_node_null;
        // Update list tail to new node
        list->tail = index;
    }
}

static void
biped_push_front(biped_linked_list_t* list, biped_node_t* node, biped_index_t index, biped_node_ctx node_ctx) biped_noexcept {
    assert(node == biped_get_node(node_ctx, index));
    // If list is empty, this node becomes both head and tail
    if (list->head == biped_node_null) {
        assert(list->tail == biped_node_null && "empty list should have null tail");
        list->head = index;
        list->tail = index;
        node->base.prev = biped_node_null;
        node->base.next = biped_node_null;
    }
    else {
        // Link new node to current head
        biped_node_t* head_node = biped_get_node(node_ctx, list->head);
        head_node->base.prev = index;
        node->base.prev = biped_node_null;
        node->base.next = list->head;
        // Update list head to new node
        list->head = index;
    }
}



// ----------------------------------------------------------------------------
//                               hash map
// ----------------------------------------------------------------------------


static bool
biped_hashmap_init(biped_openaddr_hash_t* map, uint32_t init_slot_count) biped_noexcept {
    assert(map && biped_zero_or_pow2(init_slot_count) && init_slot_count > 1);
    map->lookup_table = NULL;
    map->slot_count = 0;
    map->item_count = 0;
    const uint32_t init_slot_bytes = init_slot_count * sizeof(biped_hash_slot_t);
    biped_hash_slot_t* const ptr = (biped_hash_slot_t*)biped_malloc(init_slot_bytes);
    if (!ptr)
        return false;

    for (uint32_t i = 0; i != init_slot_count; ++i) {
        ptr[i].tag = biped_node_null;
        ptr[i].index = biped_node_null;
    }

    map->lookup_table = ptr;
    map->slot_count = init_slot_count;
    return true;
}

static void
biped_hashmap_uninit(biped_openaddr_hash_t* map) biped_noexcept {
    assert(map);
    biped_free(map->lookup_table);
    map->lookup_table = NULL;
    map->slot_count = 0;
    map->item_count = 0;

}

static bool
biped_hashmap_rehash(biped_openaddr_hash_t* map, biped_node_ctx ctx) biped_noexcept {
    assert(map && map->slot_count && map->lookup_table);
    biped_openaddr_hash_t remap;
    if (!biped_hashmap_init(&remap, map->slot_count * 2))
        return false;

    const uint32_t count = map->slot_count;
    const biped_hash_slot_t* const table = map->lookup_table;
    for (uint32_t i = 0; i != count; ++i) {
        const biped_index_t index = table[i].index;
        if (index != biped_node_null) {
            const biped_node_t* node = biped_get_node(ctx, index);
            const uint32_t hashcode = biped_hash(node->key_value, ctx.key_length);
            biped_hashmap_insert(&remap, index, hashcode);
        }
    }

    biped_hashmap_uninit(map);
    *map = remap;
    return true;
}

static void
biped_hashmap_insert(biped_openaddr_hash_t* map, biped_index_t index, uint32_t hashcode) biped_noexcept {
    assert(map && map->item_count < map->slot_count);
    const uint32_t mask = map->slot_count - 1;
    const uint32_t count = map->slot_count;
    biped_hash_slot_t* const slots = map->lookup_table;
    for (uint32_t i = 0; i != count; ++i) {
        const uint32_t hashcodei = hashcode + i;
        const uint32_t j = hashcodei & mask;
        assert(slots[j].index != index && "already exists");
        if (slots[j].index == biped_node_null) {
            slots[j].index = index;
            const biped_index_t tag = hashcode >> (sizeof(biped_index_t) * CHAR_BIT);
            slots[j].tag = tag;
            ++map->item_count;
            return;
        }
    }
    assert(!"bad path");
}

static bool
biped_hashmap_try_rehash(biped_openaddr_hash_t* map, biped_node_ctx ctx) biped_noexcept {
    // 0.5 load
    if (map->item_count * 2 >= map->slot_count) {
        if (!biped_hashmap_rehash(map, ctx))
            return false;
    }
    return true;
}


static bool
biped_hashmap_try_remove(biped_openaddr_hash_t* map, biped_node_t* node, biped_index_t index, biped_node_ctx ctx) biped_noexcept {
    // not exist
    if (!node->flag)
        return false;
    assert(index != biped_node_null);

    // Calculate hashcode from node's key
    const uint32_t hashcode = biped_hash(node->key_value, ctx.key_length);
    const uint32_t mask = map->slot_count - 1;
    const uint32_t count = map->slot_count;
    const biped_hash_slot_t* const slots = map->lookup_table;

    // Sequential search from hash position
    for (uint32_t i = 0; i != count; ++i) {
        const uint32_t hashcodei = hashcode + i;
        const uint32_t j = hashcodei & mask;
        if (slots[j].index == biped_node_null)
            break;
        if (slots[j].index == index) {
            node->flag = 0;
            biped_hashmap_remove(map, j, ctx);
            return true;
        }
    }

    assert(!"bad path");
    return false;
}


static void
biped_hashmap_remove(biped_openaddr_hash_t* map, uint32_t slot_index, biped_node_ctx ctx) biped_noexcept {
    assert(map && slot_index < map->slot_count);
    // Knuth's Algorithm R (Knuth TAOCPv3) 
    // O(K) K = Clustering

    const uint32_t mask = map->slot_count - 1;
    biped_hash_slot_t* const slots = map->lookup_table;
    uint32_t i = slot_index;
    uint32_t j = i;

    assert(slots[slot_index].index != biped_node_null);

    while (1) {
        j = (uint32_t)(j + 1) & mask;
        if (j == i || slots[j].index == biped_node_null)
            break;

        const biped_node_t* node = biped_get_node(ctx, slots[j].index);
        const uint32_t hashcode = biped_hash(node->key_value, ctx.key_length);
        const uint32_t k = hashcode & mask;

        if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) {
            slots[i] = slots[j];
            i = j;
        }
    }

    slots[i].index = biped_node_null;
    slots[i].tag = biped_node_null;

    --map->item_count;

}


static biped_hashmap_search_result
biped_hashmap_search(const biped_openaddr_hash_t* map, const uint32_t key[], biped_node_ctx ctx) biped_noexcept {
    assert(map && key && ctx.key_length);
    biped_hashmap_search_result r = { 0, biped_node_null };
    const uint32_t hashcode = biped_hash(key, ctx.key_length);
    const uint32_t mask = map->slot_count - 1;
    const uint32_t count = map->slot_count;
    const biped_hash_slot_t* const slots = map->lookup_table;
    const biped_index_t tag = hashcode >> (sizeof(biped_index_t) * CHAR_BIT);
    const size_t size_of_key = ctx.key_length * sizeof(uint32_t);
    for (uint32_t i = 0; i != count; ++i) {
        const uint32_t hashcodei = hashcode + i;
        const uint32_t j = hashcodei & mask;
        if (slots[j].index == biped_node_null)
            break;
        if (slots[j].tag == tag) {
            const biped_node_t* node = biped_get_node(ctx, slots[j].index);
            if (!memcmp(node->key_value, key, size_of_key)) {
                r.slot = j;
                r.index = slots[j].index;
                break;
            }
        }
    }
    return r;
}

// ----------------------------------------------------------------------------
//                               hash impl
// ----------------------------------------------------------------------------


// Author: Wang Yi <godspeed_china@yeah.net>
#include <stdint.h>
static inline void _wymix32(uint32_t* A, uint32_t* B) biped_noexcept {
    uint64_t  c = *A ^ 0x53c5ca59u;  c *= *B ^ 0x74743c1bu;
    *A = (uint32_t)c;
    *B = (uint32_t)(c >> 32);
}

// Optimized version for 32-bit aligned uint32_t arrays
// seealso: https://github.com/wangyi-fudan/wyhash
static inline uint32_t biped_wyhash32(const uint32_t* key, uint32_t count, uint32_t seed) biped_noexcept {
    uint32_t i = count;
    uint32_t see1 = count * sizeof(uint32_t);
    seed ^= 0; // count >> 32 is always 0 for uint32_t
    _wymix32(&seed, &see1);
    for (; i > 2; i -= 2, key += 2) {
        seed ^= key[0];
        see1 ^= key[1];
        _wymix32(&seed, &see1);
    }
    if (i > 0) {
        seed ^= key[0];
        see1 ^= key[i - 1];
    }
    _wymix32(&seed, &see1);
    _wymix32(&seed, &see1);
    return seed ^ see1;
}

uint32_t biped_hash_impl(const uint32_t key[], uint32_t count) biped_noexcept {
    const uint32_t code = biped_wyhash32(key, count, 0);
    return code;
}


// ----------------------------------------------------------------------------
//                               inner function
// ----------------------------------------------------------------------------


biped_result_t
biped_cache_get_info(biped_cache_ctx_t ctx, biped_index_t id, biped_block_info_t* info) biped_noexcept {
    assert(ctx && info);
    const biped_node_ctx node_ctx = biped_get_node_ctx(ctx);
    biped_node_t* const node = biped_get_node(node_ctx, id);

    const uint32_t length_of_key = ctx->length_of_key;
    info->position = node->pos;
    info->aligned_size = biped_to_size(node->level);
    info->real_size = node->real;
    info->id = id;
    info->value = node->key_value + length_of_key;

    return biped_result_valid;
}


uint32_t* 
biped_shrink_size(biped_cache_ctx_t ctx, biped_index_t id, biped_size2d_t size) biped_noexcept {
    const biped_node_ctx node_ctx = biped_get_node_ctx(ctx);
    biped_node_t* const node = biped_get_node(node_ctx, id);
    if (node->real.w >= size.w && node->real.h >= size.h) {
        node->real = size;
    }
    return node->key_value;

}



#endif