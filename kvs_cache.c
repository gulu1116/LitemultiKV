
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

/* ====================== 增强版统计结构 ====================== */
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
