/********************************************************************
 *  LitemultiKV - Cache Eviction Engine (LRU / LFU / ARC)
 *
 *  智能缓存淘汰策略层，为所有存储引擎提供：
 *    - LRU (Least Recently Used)
 *    - LFU (Least Frequently Used)
 *    - ARC (Adaptive Replacement Cache) - 双缓冲区自适应
 *    - 访问模式统计与 AI 调优建议
 *
 *  命令:
 *    POLICY SET <lru|lfu|arc>    设置淘汰策略
 *    POLICY                       查询当前策略
 *    STATS                        打印全局统计
 *    RECOMMEND                    AI 调优建议
 *
 *  编译条件: ENABLE_CACHE=1 (kvstore.h)
 ********************************************************************/

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

/* ====================== AI 辅助函数 ====================== */
static float exponential_smooth(float *history, int count, float alpha)
{
    if (count == 0)
        return 0.0f;

    float prediction = history[0];
    for (int i = 1; i < count && i < HISTORY_SIZE; i++)
    {
        prediction = alpha * history[i] + (1.0f - alpha) * prediction;
    }
    return prediction;
}

static float calculate_variance(float *data, int count, float mean)
{
    if (count <= 1)
        return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < count && i < HISTORY_SIZE; i++)
    {
        float diff = data[i] - mean;
        sum += diff * diff;
    }
    return sum / (count - 1);
}

static AccessPattern detect_access_pattern(CacheManager *cm)
{
    AIStats *ai = &cm->ai;

    if (ai->history_count < 5)
    {
        return PATTERN_STABLE;
    }

    float mean = 0.0f;
    for (int i = 0; i < ai->history_count && i < HISTORY_SIZE; i++)
    {
        mean += ai->hit_rate_history[i];
    }
    mean /= ai->history_count;

    float variance = calculate_variance(ai->hit_rate_history, ai->history_count, mean);
    float std_dev = sqrtf(variance);

    unsigned long peak_qps = 0, avg_qps = 0, total = 0;
    for (int i = 0; i < 60; i++)
    {
        total += cm->access_by_second[i];
        if (cm->access_by_second[i] > peak_qps)
        {
            peak_qps = cm->access_by_second[i];
        }
    }
    avg_qps = total / 60;
    float burst_ratio = (avg_qps > 0) ? ((float)peak_qps / avg_qps) : 1.0f;

    if (burst_ratio > 3.0f)
    {
        return PATTERN_BURSTY;
    }

    if (std_dev < 5.0f && mean > 60.0f)
    {
        return PATTERN_STABLE;
    }

    if (std_dev > 15.0f)
    {
        return PATTERN_RANDOM;
    }

    return PATTERN_PERIODIC;
}

static void update_dynamic_thresholds(CacheManager *cm)
{
    AIStats *ai = &cm->ai;

    if (ai->history_count < 3)
        return;

    float mean = 0.0f;
    for (int i = 0; i < ai->history_count && i < HISTORY_SIZE; i++)
    {
        mean += ai->hit_rate_history[i];
    }
    mean /= ai->history_count;

    float variance = calculate_variance(ai->hit_rate_history, ai->history_count, mean);
    float std_dev = sqrtf(variance);

    ai->threshold_hit_rate = mean - std_dev;
    if (ai->threshold_hit_rate < 30.0f)
        ai->threshold_hit_rate = 30.0f;
    if (ai->threshold_hit_rate > 70.0f)
        ai->threshold_hit_rate = 70.0f;

    ai->threshold_burst_ratio = 3.0f + (std_dev / 10.0f);
    if (ai->threshold_burst_ratio > 8.0f)
        ai->threshold_burst_ratio = 8.0f;
    if (ai->threshold_burst_ratio < 2.0f)
        ai->threshold_burst_ratio = 2.0f;
}

static void record_hit_rate_sample(CacheManager *cm, float hit_rate)
{
    AIStats *ai = &cm->ai;

    ai->hit_rate_history[ai->history_idx] = hit_rate;
    ai->history_idx = (ai->history_idx + 1) % HISTORY_SIZE;
    if (ai->history_count < HISTORY_SIZE)
    {
        ai->history_count++;
    }
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

/* ====================== 缓存管理器 API ====================== */
int kvs_cache_init(int max_items)
{
    memset(&g_cache, 0, sizeof(CacheManager));

    g_cache.policy = EVICT_LRU;
    g_cache.max_items = max_items > 0 ? max_items : DEFAULT_MAX_ITEMS;

    lru_init(&g_cache.lru, g_cache.max_items);
    lfu_init(&g_cache.lfu, g_cache.max_items);
    arc_init(&g_cache.arc, g_cache.max_items);

    g_cache.ai.threshold_hit_rate = 50.0f;
    g_cache.ai.threshold_burst_ratio = 5.0f;
    g_cache.ai.auto_adjust_enabled = 1;
    g_cache.ai.adjust_cooldown = 60;
    g_cache.ai.pattern_confidence = CONFIDENCE_MEDIUM;

    return 0;
}

void kvs_cache_destroy(void)
{
    /* 释放 LRU 链表 */
    EvictNode *node = g_cache.lru.head;
    while (node)
    {
        EvictNode *next = node->next;
        kvs_free(node->key);
        kvs_free(node->value);
        kvs_free(node);
        node = next;
    }
    g_cache.lru.head = NULL;
    g_cache.lru.tail = NULL;

    /* 释放 LFU 堆中的节点 */
    if (g_cache.lfu.nodes)
    {
        for (int i = 1; i <= g_cache.lfu.size; i++)
        {
            if (g_cache.lfu.nodes[i])
            {
                kvs_free(g_cache.lfu.nodes[i]->key);
                kvs_free(g_cache.lfu.nodes[i]->value);
                kvs_free(g_cache.lfu.nodes[i]);
            }
        }
        kvs_free(g_cache.lfu.nodes);
        g_cache.lfu.nodes = NULL;
    }

    /* 释放 ARC 四个链表 */
    LRUCache *arc_lists[] = {g_cache.arc.t1, g_cache.arc.t2, g_cache.arc.b1, g_cache.arc.b2};
    for (int i = 0; i < 4; i++)
    {
        if (!arc_lists[i])
            continue;
        EvictNode *n = arc_lists[i]->head;
        while (n)
        {
            EvictNode *next = n->next;
            kvs_free(n->key);
            kvs_free(n->value);
            kvs_free(n);
            n = next;
        }
        kvs_free(arc_lists[i]);
    }
    g_cache.arc.t1 = NULL;
    g_cache.arc.t2 = NULL;
    g_cache.arc.b1 = NULL;
    g_cache.arc.b2 = NULL;
}

static void update_time_windows(CacheManager *cm)
{
    unsigned long now = current_time_sec();
    AIStats *ai = &cm->ai;

    if (ai->last_minute == 0)
    {
        ai->last_minute = now / 60;
        ai->last_hour = now / 3600;
        return;
    }

    unsigned long current_minute = now / 60;
    unsigned long current_hour = now / 3600;

    if (current_minute > ai->last_minute)
    {
        int shift = (int)(current_minute - ai->last_minute);
        if (shift >= MINUTE_WINDOW)
        {
            memset(ai->access_by_minute, 0, sizeof(ai->access_by_minute));
        }
        else
        {
            for (int i = MINUTE_WINDOW - 1; i >= shift; i--)
            {
                ai->access_by_minute[i] = ai->access_by_minute[i - shift];
            }
            for (int i = 0; i < shift && i < MINUTE_WINDOW; i++)
            {
                ai->access_by_minute[i] = 0;
            }
        }
        ai->last_minute = current_minute;
    }

    if (current_hour > ai->last_hour)
    {
        int shift = (int)(current_hour - ai->last_hour);
        if (shift >= HOUR_WINDOW)
        {
            memset(ai->access_by_hour, 0, sizeof(ai->access_by_hour));
        }
        else
        {
            for (int i = HOUR_WINDOW - 1; i >= shift; i--)
            {
                ai->access_by_hour[i] = ai->access_by_hour[i - shift];
            }
            for (int i = 0; i < shift && i < HOUR_WINDOW; i++)
            {
                ai->access_by_hour[i] = 0;
            }
        }
        ai->last_hour = current_hour;
    }
}

void kvs_cache_record_access(const char *key)
{
    unsigned long now = current_time_ms();
    g_cache.total_access++;

    unsigned long second = now / 1000;
    if (g_cache.last_update_time == 0)
        g_cache.last_update_time = second;

    if (second > g_cache.last_update_time)
    {
        int shift = (int)(second - g_cache.last_update_time);
        if (shift > 60)
        {
            memset(g_cache.access_by_second, 0, sizeof(g_cache.access_by_second));
        }
        else
        {
            for (int i = 59; i >= shift; i--)
            {
                g_cache.access_by_second[i] = g_cache.access_by_second[i - shift];
            }
            for (int i = 0; i < shift && i < 60; i++)
            {
                g_cache.access_by_second[i] = 0;
            }
        }
        g_cache.last_update_time = second;

        float current_hit_rate = (g_cache.total_access > 0)
                                     ? (100.0f * g_cache.cache_hit / g_cache.total_access)
                                     : 0.0f;
        record_hit_rate_sample(&g_cache, current_hit_rate);

        update_dynamic_thresholds(&g_cache);
    }

    int sec_idx = (int)(second % 60);
    g_cache.access_by_second[sec_idx]++;

    update_time_windows(&g_cache);

    unsigned long current_minute = current_time_sec() / 60;
    int min_idx = (int)(current_minute % MINUTE_WINDOW);
    g_cache.ai.access_by_minute[min_idx]++;

    unsigned long current_hour = current_time_sec() / 3600;
    int hour_idx = (int)(current_hour % HOUR_WINDOW);
    g_cache.ai.access_by_hour[hour_idx]++;

    (void)key;
}

void kvs_cache_touch_lru(const char *key)
{
    EvictNode *node = g_cache.lru.head;
    while (node)
    {
        if (strcmp(node->key, key) == 0)
        {
            lru_touch(&g_cache.lru, node);
            break;
        }
        node = node->next;
    }
}

char *kvs_cache_lru_evict_key(void)
{
    EvictNode *victim = lru_evict(&g_cache.lru);
    if (victim)
    {
        g_cache.eviction_count++;
        char *evicted_key = victim->key;
        kvs_free(victim->value);
        kvs_free(victim);
        return evicted_key;
    }
    return NULL;
}

int kvs_cache_lru_need_evict(void)
{
    return (g_cache.lru.size >= g_cache.max_items);
}

void kvs_cache_lru_add(const char *key, void *value, int value_len)
{
    if (g_cache.policy == EVICT_ARC)
    {
        arc_add(&g_cache.arc, key, value, value_len);
        return;
    }

    EvictNode *node = (EvictNode *)kvs_malloc(sizeof(EvictNode));
    node->key = kvs_malloc(strlen(key) + 1);
    strcpy(node->key, key);
    node->value = kvs_malloc(value_len);
    memcpy(node->value, value, value_len);
    node->value_len = value_len;
    node->access_time = current_time_ms();
    node->access_count = 1;
    node->prev = node->next = NULL;

    if (g_cache.policy == EVICT_LRU)
    {
        while (g_cache.lru.size >= g_cache.max_items)
        {
            char *ek = kvs_cache_lru_evict_key();
            if (ek)
            {
                kvs_free(ek);
            }
        }
        lru_add_front(&g_cache.lru, node);
    }
    else if (g_cache.policy == EVICT_LFU)
    {
        if (g_cache.lfu.size >= g_cache.lfu.capacity)
        {
            EvictNode *victim = lfu_evict(&g_cache.lfu);
            if (victim)
            {
                kvs_free(victim->key);
                kvs_free(victim->value);
                kvs_free(victim);
            }
        }
        lfu_add(&g_cache.lfu, node);
    }
}

const char *kvs_cache_policy_name(int policy)
{
    switch (policy)
    {
    case EVICT_LRU:
        return "LRU";
    case EVICT_LFU:
        return "LFU";
    case EVICT_ARC:
        return "ARC";
    default:
        return "UNKNOWN";
    }
}

static const char *pattern_name(AccessPattern pattern)
{
    switch (pattern)
    {
    case PATTERN_STABLE:
        return "Stable";
    case PATTERN_BURSTY:
        return "Bursty";
    case PATTERN_PERIODIC:
        return "Periodic";
    case PATTERN_RANDOM:
        return "Random";
    default:
        return "Unknown";
    }
}

static const char *confidence_name(int confidence)
{
    switch (confidence)
    {
    case CONFIDENCE_HIGH:
        return "High";
    case CONFIDENCE_MEDIUM:
        return "Medium";
    case CONFIDENCE_LOW:
        return "Low";
    default:
        return "Unknown";
    }
}

void kvs_cache_set_policy(int policy)
{
    g_cache.policy = policy;
    g_cache.policy_changes++;
}

int kvs_cache_get_policy(void)
{
    return g_cache.policy;
}

void kvs_cache_get_stats(char *out, int max_len)
{
    float hit_rate = (g_cache.total_access > 0)
                         ? (100.0f * g_cache.cache_hit / g_cache.total_access)
                         : 0.0f;

    unsigned long recent_qps = 0;
    for (int i = 0; i < 60; i++)
        recent_qps += g_cache.access_by_second[i];
    recent_qps /= 60;

    unsigned long minute_total = 0;
    for (int i = 0; i < MINUTE_WINDOW; i++)
    {
        minute_total += g_cache.ai.access_by_minute[i];
    }
    unsigned long avg_per_minute = minute_total / MINUTE_WINDOW;

    int len;
    if (g_cache.policy == EVICT_ARC)
    {
        len = snprintf(out, max_len,
                       "=== Cache Statistics (Enhanced) ===\r\n"
                       "  Policy: %s\r\n"
                       "  Max Items: %d\r\n"
                       "  T1 Size: %d (recent)\r\n"
                       "  T2 Size: %d (frequent)\r\n"
                       "  B1 Size: %d (ghost recent)\r\n"
                       "  B2 Size: %d (ghost frequent)\r\n"
                       "  Target T1 Size: %d\r\n"
                       "  Total Access: %lu\r\n"
                       "  Cache Hits: %lu\r\n"
                       "  Cache Miss: %lu\r\n"
                       "  Hit Rate: %.2f%%\r\n"
                       "  Evictions: %lu\r\n"
                       "  Policy Changes: %lu\r\n"
                       "  Recent QPS: ~%lu/s\r\n"
                       "  Avg Access/Min: ~%lu\r\n"
                       "  Detected Pattern: %s\r\n"
                       "  Dynamic Threshold (Hit Rate): %.1f%%\r\n"
                       "  Dynamic Threshold (Burst): %.1fx\r\n"
                       "===================================\r\n",
                       kvs_cache_policy_name(g_cache.policy),
                       g_cache.max_items,
                       g_cache.arc.t1->size,
                       g_cache.arc.t2->size,
                       g_cache.arc.b1->size,
                       g_cache.arc.b2->size,
                       g_cache.arc.target_size,
                       g_cache.total_access,
                       g_cache.cache_hit,
                       g_cache.cache_miss,
                       hit_rate,
                       g_cache.eviction_count,
                       g_cache.policy_changes,
                       recent_qps,
                       avg_per_minute,
                       pattern_name(g_cache.ai.detected_pattern),
                       g_cache.ai.threshold_hit_rate,
                       g_cache.ai.threshold_burst_ratio);
    }
    else
    {
        len = snprintf(out, max_len,
                       "=== Cache Statistics (Enhanced) ===\r\n"
                       "  Policy: %s\r\n"
                       "  Max Items: %d\r\n"
                       "  Cache Size: %d\r\n"
                       "  Total Access: %lu\r\n"
                       "  Cache Hits: %lu\r\n"
                       "  Cache Miss: %lu\r\n"
                       "  Hit Rate: %.2f%%\r\n"
                       "  Evictions: %lu\r\n"
                       "  Policy Changes: %lu\r\n"
                       "  Recent QPS: ~%lu/s\r\n"
                       "  Avg Access/Min: ~%lu\r\n"
                       "  Detected Pattern: %s\r\n"
                       "  Dynamic Threshold (Hit Rate): %.1f%%\r\n"
                       "  Dynamic Threshold (Burst): %.1fx\r\n"
                       "===================================\r\n",
                       kvs_cache_policy_name(g_cache.policy),
                       g_cache.max_items,
                       g_cache.lru.size,
                       g_cache.total_access,
                       g_cache.cache_hit,
                       g_cache.cache_miss,
                       hit_rate,
                       g_cache.eviction_count,
                       g_cache.policy_changes,
                       recent_qps,
                       avg_per_minute,
                       pattern_name(g_cache.ai.detected_pattern),
                       g_cache.ai.threshold_hit_rate,
                       g_cache.ai.threshold_burst_ratio);
    }
    (void)len;
}

void kvs_cache_ai_recommend(char *out, int max_len)
{
    unsigned long peak_qps = 0, avg_qps = 0, total = 0;
    for (int i = 0; i < 60; i++)
    {
        unsigned long v = g_cache.access_by_second[i];
        total += v;
        if (v > peak_qps)
            peak_qps = v;
    }
    avg_qps = total / 60;

    float hit_rate = (g_cache.total_access > 0)
                         ? (100.0f * g_cache.cache_hit / g_cache.total_access)
                         : 0.0f;

    float peak_to_avg = (avg_qps > 0) ? ((float)peak_qps / avg_qps) : 0.0f;

    g_cache.ai.detected_pattern = detect_access_pattern(&g_cache);

    g_cache.ai.predicted_hit_rate = exponential_smooth(
        g_cache.ai.hit_rate_history,
        g_cache.ai.history_count,
        SMOOTH_ALPHA);

    g_cache.ai.predicted_qps = (float)avg_qps * 1.1f;

    int recommended_policy = g_cache.policy;
    int recommended_max = g_cache.max_items;
    int confidence = CONFIDENCE_MEDIUM;
    const char *reason = "";
    const char *detail = "";

    if (hit_rate < g_cache.ai.threshold_hit_rate)
    {
        recommended_policy = EVICT_LFU;
        recommended_max = g_cache.max_items * 2;
        confidence = CONFIDENCE_HIGH;
        reason = "Low hit rate detected";
        detail = "LFU better identifies persistent hot keys when hit rate is low";
    }
    else if (peak_to_avg > g_cache.ai.threshold_burst_ratio)
    {
        recommended_policy = EVICT_ARC;
        recommended_max = g_cache.max_items * 2;
        confidence = CONFIDENCE_HIGH;
        reason = "Bursty access pattern detected";
        detail = "ARC dual-buffer adapts well to sudden traffic spikes";
    }
    else if (g_cache.ai.detected_pattern == PATTERN_PERIODIC)
    {
        recommended_policy = EVICT_ARC;
        confidence = CONFIDENCE_MEDIUM;
        reason = "Periodic access pattern detected";
        detail = "ARC adapts well to time-varying access patterns";
    }
    else if (g_cache.ai.detected_pattern == PATTERN_RANDOM)
    {
        recommended_policy = EVICT_LRU;
        confidence = CONFIDENCE_LOW;
        reason = "Random access pattern detected";
        detail = "LRU provides best overall performance for random access";
    }
    else if (g_cache.policy_changes > 3)
    {
        recommended_policy = EVICT_LRU;
        confidence = CONFIDENCE_MEDIUM;
        reason = "Frequent policy changes detected";
        detail = "Stick with LRU for stability";
    }
    else
    {
        confidence = CONFIDENCE_HIGH;
        reason = "Current configuration is optimal";
        detail = "No changes needed - system performing well";
    }

    int len = snprintf(out, max_len,
                       "=== AI Cache Recommendation (Enhanced) ===\r\n"
                       "\r\n"
                       "[Current Status]\r\n"
                       "  Policy: %s\r\n"
                       "  Hit Rate: %.2f%% (Predicted: %.1f%%)\r\n"
                       "  Peak QPS: %lu, Avg QPS: %lu\r\n"
                       "  Peak/Avg Ratio: %.2fx\r\n"
                       "  Predicted QPS: %.0f\r\n"
                       "\r\n"
                       "[Pattern Analysis]\r\n"
                       "  Detected Pattern: %s\r\n"
                       "  Dynamic Hit Rate Threshold: %.1f%%\r\n"
                       "  Dynamic Burst Threshold: %.1fx\r\n"
                       "  History Samples: %d\r\n"
                       "\r\n"
                       "[Recommendation]\r\n"
                       "  Recommended Policy: %s\r\n"
                       "  Recommended Max Items: %d\r\n"
                       "  Confidence: %s\r\n"
                       "  Reason: %s\r\n"
                       "  Detail: %s\r\n"
                       "\r\n"
                       "[Actions]\r\n"
                       "  To apply: POLICY SET %s\r\n"
                       "  To view stats: STATS\r\n"
                       "=========================================\r\n",
                       kvs_cache_policy_name(g_cache.policy),
                       hit_rate,
                       g_cache.ai.predicted_hit_rate,
                       peak_qps, avg_qps,
                       peak_to_avg,
                       g_cache.ai.predicted_qps,
                       pattern_name(g_cache.ai.detected_pattern),
                       g_cache.ai.threshold_hit_rate,
                       g_cache.ai.threshold_burst_ratio,
                       g_cache.ai.history_count,
                       kvs_cache_policy_name(recommended_policy),
                       recommended_max,
                       confidence_name(confidence),
                       reason,
                       detail,
                       kvs_cache_policy_name(recommended_policy));
    (void)len;
}

void kvs_cache_record_hit(void)
{
    g_cache.cache_hit++;
}

void kvs_cache_record_miss(void)
{
    g_cache.cache_miss++;
}

void *kvs_cache_get(const char *key, int *value_len)
{
    if (!key || !g_cache.max_items)
        return NULL;

    EvictNode *node = NULL;

    switch (g_cache.policy)
    {
    case EVICT_LRU:
        for (node = g_cache.lru.head; node; node = node->next)
        {
            if (node->key && strcmp(node->key, key) == 0)
            {
                lru_touch(&g_cache.lru, node);
                g_cache.cache_hit++;
                g_cache.total_access++;
                if (value_len)
                    *value_len = node->value_len;
                return node->value;
            }
        }
        break;

    case EVICT_LFU:
        for (int i = 1; i <= g_cache.lfu.size; i++)
        {
            node = g_cache.lfu.nodes[i];
            if (node && node->key && strcmp(node->key, key) == 0)
            {
                node->access_count++;
                lfu_heapify_down(&g_cache.lfu, i);
                g_cache.cache_hit++;
                g_cache.total_access++;
                if (value_len)
                    *value_len = node->value_len;
                return node->value;
            }
        }
        break;

    case EVICT_ARC:
    {
        void *result = arc_get(&g_cache.arc, key, value_len);
        if (result)
        {
            g_cache.cache_hit++;
        }
        else
        {
            g_cache.cache_miss++;
        }
        g_cache.total_access++;
        return result;
    }
    }

    g_cache.cache_miss++;
    g_cache.total_access++;
    return NULL;
}

int kvs_cache_del(const char *key)
{
    if (!key || !g_cache.max_items)
        return -1;

    EvictNode *node = NULL;

    switch (g_cache.policy)
    {
    case EVICT_LRU:
        for (node = g_cache.lru.head; node; node = node->next)
        {
            if (node->key && strcmp(node->key, key) == 0)
            {
                lru_remove(&g_cache.lru, node);
                kvs_free(node->key);
                kvs_free(node->value);
                kvs_free(node);
                return 0;
            }
        }
        break;

    case EVICT_LFU:
        for (int i = 1; i <= g_cache.lfu.size; i++)
        {
            node = g_cache.lfu.nodes[i];
            if (node && node->key && strcmp(node->key, key) == 0)
            {
                EvictNode *last = g_cache.lfu.nodes[g_cache.lfu.size];
                g_cache.lfu.nodes[i] = last;
                g_cache.lfu.size--;

                lfu_heapify_down(&g_cache.lfu, i);

                kvs_free(node->key);
                kvs_free(node->value);
                kvs_free(node);
                return 0;
            }
        }
        break;

    case EVICT_ARC:
        return arc_del(&g_cache.arc, key);
    }

    return -2;
}

#endif /* ENABLE_CACHE */
