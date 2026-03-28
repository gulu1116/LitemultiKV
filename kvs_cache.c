
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <math.h>
#include "kvstore.h"

#ifdef ENABLE_CACHE

/* ====================== 淘汰策略类型 ====================== */
#define EVICT_LRU 0
#define EVICT_LFU 1
#define EVICT_ARC 2

#define DEFAULT_MAX_ITEMS 1000

/* ====================== 增强版 AI 参数 ====================== */
#define HISTORY_SIZE 20
#define MINUTE_WINDOW 60
#define HOUR_WINDOW 24

#define CONFIDENCE_HIGH 3
#define CONFIDENCE_MEDIUM 2
#define CONFIDENCE_LOW 1

#define SMOOTH_ALPHA 0.3f

typedef enum
{
    PATTERN_STABLE = 0,
    PATTERN_BURSTY = 1,
    PATTERN_PERIODIC = 2,
    PATTERN_RANDOM = 3
} AccessPattern;

/* ====================== 链表节点 ====================== */
typedef struct EvictNode
{
    char *key;
    void *value;
    int value_len;

    /* LRU 字段 */
    struct EvictNode *prev;
    struct EvictNode *next;
    unsigned long access_time;

    /* LFU 字段 */
    unsigned long access_count;

    /* ARC 字段 */
    int is_frequent;

} EvictNode;

/* ====================== LRU 链表 ====================== */
typedef struct
{
    EvictNode *head;
    EvictNode *tail;
    int size;
    int max_size;
} LRUCache;

/* ====================== LFU 最小堆 ====================== */
typedef struct
{
    EvictNode **nodes;
    int size;
    int capacity;
} LFUCache;

/* ====================== ARC 双缓冲 ====================== */
typedef struct
{
    LRUCache *t1;
    LRUCache *t2;
    LRUCache *b1;
    LRUCache *b2;
    int target_size;
    int max_size;
} ARCCache;

/* ====================== 统计结构 ====================== */
typedef struct
{
    float hit_rate_history[HISTORY_SIZE];
    int history_idx;
    int history_count;

    float threshold_hit_rate;
    float threshold_burst_ratio;

    unsigned long access_by_minute[MINUTE_WINDOW];
    unsigned long access_by_hour[HOUR_WINDOW];

    float predicted_qps;
    float predicted_hit_rate;

    AccessPattern detected_pattern;
    int pattern_confidence;

    unsigned long last_minute;
    unsigned long last_hour;

    int auto_adjust_enabled;
    unsigned long last_adjust_time;
    int adjust_cooldown;
} AIStats;

/* ====================== 全局缓存管理 ====================== */
typedef struct
{
    int policy;
    int max_items;

    LRUCache lru;
    LFUCache lfu;
    ARCCache arc;

    /* 统计信息 */
    unsigned long total_access;
    unsigned long cache_hit;
    unsigned long cache_miss;
    unsigned long eviction_count;
    unsigned long policy_changes;

    /* 访问模式分析 */
    unsigned long access_by_second[60];
    unsigned long last_update_time;

    /* AI 统计 */
    AIStats ai;
} CacheManager;

static CacheManager g_cache = {0};

/* ====================== 工具函数 ====================== */
static unsigned long current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static unsigned long current_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec;
}

/* ====================== LRU 实现 ====================== */
static void lru_init(LRUCache *cache, int max_size)
{
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    cache->max_size = max_size;
}

static void lru_remove(LRUCache *cache, EvictNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        cache->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        cache->tail = node->prev;

    cache->size--;
}

static void lru_add_front(LRUCache *cache, EvictNode *node)
{
    node->next = cache->head;
    node->prev = NULL;

    if (cache->head)
        cache->head->prev = node;
    cache->head = node;

    if (!cache->tail)
        cache->tail = node;

    cache->size++;
}

static void lru_touch(LRUCache *cache, EvictNode *node)
{
    lru_remove(cache, node);
    lru_add_front(cache, node);
    node->access_time = current_time_ms();
}

static EvictNode *lru_evict(LRUCache *cache)
{
    if (!cache->tail)
        return NULL;
    EvictNode *victim = cache->tail;
    lru_remove(cache, victim);
    return victim;
}

/* ====================== LFU 最小堆实现 ====================== */
static void lfu_init(LFUCache *cache, int capacity)
{
    cache->nodes = (EvictNode **)calloc(capacity + 1, sizeof(EvictNode *));
    cache->size = 0;
    cache->capacity = capacity;
}

static void lfu_swap(LFUCache *cache, int i, int j)
{
    EvictNode *tmp = cache->nodes[i];
    cache->nodes[i] = cache->nodes[j];
    cache->nodes[j] = tmp;
}

static void lfu_heapify_up(LFUCache *cache, int idx)
{
    while (idx > 1)
    {
        int parent = idx / 2;
        if (cache->nodes[parent]->access_count <= cache->nodes[idx]->access_count)
            break;
        lfu_swap(cache, parent, idx);
        idx = parent;
    }
}

static void lfu_heapify_down(LFUCache *cache, int idx)
{
    while (1)
    {
        int smallest = idx;
        int left = idx * 2;
        int right = idx * 2 + 1;

        if (left <= cache->size && cache->nodes[left]->access_count < cache->nodes[smallest]->access_count)
            smallest = left;
        if (right <= cache->size && cache->nodes[right]->access_count < cache->nodes[smallest]->access_count)
            smallest = right;

        if (smallest == idx)
            break;

        lfu_swap(cache, smallest, idx);
        idx = smallest;
    }
}

static void lfu_add(LFUCache *cache, EvictNode *node)
{
    if (cache->size >= cache->capacity)
        return;

    cache->size++;
    cache->nodes[cache->size] = node;
    lfu_heapify_up(cache, cache->size);
}

static void lfu_touch(LFUCache *cache, EvictNode *node)
{
    node->access_count++;
    (void)cache;
}

static EvictNode *lfu_evict(LFUCache *cache)
{
    if (cache->size == 0)
        return NULL;
    EvictNode *victim = cache->nodes[1];
    cache->nodes[1] = cache->nodes[cache->size];
    cache->size--;
    lfu_heapify_down(cache, 1);
    return victim;
}

/* ====================== ARC 实现 ====================== */
static void arc_init(ARCCache *cache, int max_size)
{
    cache->max_size = max_size;
    cache->target_size = max_size / 2;
    cache->t1 = (LRUCache *)calloc(1, sizeof(LRUCache));
    cache->t2 = (LRUCache *)calloc(1, sizeof(LRUCache));
    cache->b1 = (LRUCache *)calloc(1, sizeof(LRUCache));
    cache->b2 = (LRUCache *)calloc(1, sizeof(LRUCache));

    lru_init(cache->t1, max_size);
    lru_init(cache->t2, max_size);
    lru_init(cache->b1, max_size);
    lru_init(cache->b2, max_size);
}

static EvictNode *lru_find(LRUCache *cache, const char *key)
{
    EvictNode *node = cache->head;
    while (node)
    {
        if (node->key && strcmp(node->key, key) == 0)
            return node;
        node = node->next;
    }
    return NULL;
}

static void lru_remove_node(LRUCache *cache, EvictNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        cache->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        cache->tail = node->prev;

    cache->size--;
}

static void lru_add_node_front(LRUCache *cache, EvictNode *node)
{
    node->next = cache->head;
    node->prev = NULL;

    if (cache->head)
        cache->head->prev = node;
    cache->head = node;

    if (!cache->tail)
        cache->tail = node;

    cache->size++;
}

#define ARC_MISS 0
#define ARC_HIT_T1 1
#define ARC_HIT_T2 2
#define ARC_HIT_B1 3
#define ARC_HIT_B2 4

static int arc_access(ARCCache *cache, const char *key)
{
    EvictNode *node = NULL;

    node = lru_find(cache->t1, key);
    if (node)
    {
        lru_remove_node(cache->t1, node);
        lru_add_node_front(cache->t2, node);
        node->is_frequent = 1;
        return ARC_HIT_T1;
    }

    node = lru_find(cache->t2, key);
    if (node)
    {
        lru_remove_node(cache->t2, node);
        lru_add_node_front(cache->t2, node);
        return ARC_HIT_T2;
    }

    node = lru_find(cache->b1, key);
    if (node)
    {
        int delta = 1;
        if (cache->b2->size > 0)
        {
            delta = cache->b2->size / cache->b1->size;
            if (delta < 1)
                delta = 1;
        }
        cache->target_size = cache->target_size + delta;
        if (cache->target_size > cache->max_size)
            cache->target_size = cache->max_size;

        lru_remove_node(cache->b1, node);
        lru_add_node_front(cache->t2, node);
        node->is_frequent = 1;
        return ARC_HIT_B1;
    }

    node = lru_find(cache->b2, key);
    if (node)
    {
        int delta = 1;
        if (cache->b1->size > 0)
        {
            delta = cache->b1->size / cache->b2->size;
            if (delta < 1)
                delta = 1;
        }
        cache->target_size = cache->target_size - delta;
        if (cache->target_size < 0)
            cache->target_size = 0;

        lru_remove_node(cache->b2, node);
        lru_add_node_front(cache->t2, node);
        node->is_frequent = 1;
        return ARC_HIT_B2;
    }

    return ARC_MISS;
}

static EvictNode *arc_evict(ARCCache *cache)
{
    int total = cache->t1->size + cache->t2->size;
    if (total < cache->max_size)
        return NULL;

    EvictNode *victim = NULL;

    if (cache->t1->size > cache->target_size)
    {
        victim = cache->t1->tail;
        if (victim)
        {
            lru_remove_node(cache->t1, victim);
            EvictNode *ghost = (EvictNode *)kvs_malloc(sizeof(EvictNode));
            ghost->key = kvs_strdup(victim->key);
            ghost->value = NULL;
            ghost->value_len = 0;
            ghost->is_frequent = 0;
            ghost->access_time = current_time_ms();
            ghost->access_count = victim->access_count;
            lru_add_node_front(cache->b1, ghost);

            while (cache->b1->size > cache->max_size)
            {
                EvictNode *old_ghost = cache->b1->tail;
                lru_remove_node(cache->b1, old_ghost);
                kvs_free(old_ghost->key);
                kvs_free(old_ghost);
            }
            return victim;
        }
    }

    victim = cache->t2->tail;
    if (victim)
    {
        lru_remove_node(cache->t2, victim);
        EvictNode *ghost = (EvictNode *)kvs_malloc(sizeof(EvictNode));
        ghost->key = kvs_strdup(victim->key);
        ghost->value = NULL;
        ghost->value_len = 0;
        ghost->is_frequent = 1;
        ghost->access_time = current_time_ms();
        ghost->access_count = victim->access_count;
        lru_add_node_front(cache->b2, ghost);

        while (cache->b2->size > cache->max_size)
        {
            EvictNode *old_ghost = cache->b2->tail;
            lru_remove_node(cache->b2, old_ghost);
            kvs_free(old_ghost->key);
            kvs_free(old_ghost);
        }
        return victim;
    }

    return NULL;
}

static void arc_add(ARCCache *cache, const char *key, void *value, int value_len)
{
    EvictNode *victim = arc_evict(cache);
    if (victim)
    {
        kvs_free(victim->key);
        kvs_free(victim->value);
        kvs_free(victim);
    }

    EvictNode *node = (EvictNode *)kvs_malloc(sizeof(EvictNode));
    node->key = kvs_strdup(key);
    node->value = kvs_malloc(value_len);
    memcpy(node->value, value, value_len);
    node->value_len = value_len;
    node->is_frequent = 0;
    node->access_time = current_time_ms();
    node->access_count = 1;
    node->prev = node->next = NULL;

    lru_add_node_front(cache->t1, node);
}

static void *arc_get(ARCCache *cache, const char *key, int *value_len)
{
    int hit_type = arc_access(cache, key);

    if (hit_type == ARC_HIT_T1 || hit_type == ARC_HIT_T2)
    {
        EvictNode *node = lru_find(cache->t2, key);
        if (!node)
            node = lru_find(cache->t1, key);
        if (node && node->value)
        {
            if (value_len)
                *value_len = node->value_len;
            return node->value;
        }
    }

    return NULL;
}

static int arc_del(ARCCache *cache, const char *key)
{
    EvictNode *node = NULL;

    node = lru_find(cache->t1, key);
    if (node)
    {
        lru_remove_node(cache->t1, node);
        kvs_free(node->key);
        kvs_free(node->value);
        kvs_free(node);
        return 0;
    }

    node = lru_find(cache->t2, key);
    if (node)
    {
        lru_remove_node(cache->t2, node);
        kvs_free(node->key);
        kvs_free(node->value);
        kvs_free(node);
        return 0;
    }

    node = lru_find(cache->b1, key);
    if (node)
    {
        lru_remove_node(cache->b1, node);
        kvs_free(node->key);
        kvs_free(node);
        return 0;
    }

    node = lru_find(cache->b2, key);
    if (node)
    {
        lru_remove_node(cache->b2, node);
        kvs_free(node->key);
        kvs_free(node);
        return 0;
    }

    return -1;
}
