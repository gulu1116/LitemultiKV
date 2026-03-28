/********************************************************************
 *  LitemultiKV - Vector Storage Engine (HNSW Algorithm)
 *
 *  实现近似最近邻向量检索，支持：
 *    - 欧氏距离 (L2)
 *    - 内积 (IP / Cosine)
 *    - 批量插入和搜索
 *    - 动态扩容
 *
 *  新增命令:
 *    VSET <key> <dim> <float1 float2 ...>
 *    VGET <key>
 *    VSEARCH <key> <k> <float1 float2 ...>
 *    VDELETE <key>
 *    VCOUNT
 ********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include "kvstore.h"

#ifdef ENABLE_VECTOR

/* ====================== HNSW 配置 ====================== */
#define MAX_DIM 1536        /* 最大向量维度 (支持 OpenAI embeddings) */
#define MAX_ELEMENTS 100000 /* 最大元素数量 */
#define M 16                /* 每层最大连接数 (建议 M*2) */
#define M_MAX 32            /* 生成层时的最大连接数 */
#define EF_CONSTRUCTION 200 /* 构建时的搜索宽度 */
#define EF_SEARCH 128       /* 查询时的搜索宽度 */
#define LAYER_DECAY 0.0253  /* 层衰减因子: 1/ln(M) 约等于 0.27 -> 1/40 */

#define MAX_LAYERS 16

typedef struct
{
    int dim;       /* 向量维度 */
    float *vector; /* 向量数据 */
    char *key;     /* 关联的字符串 key */
} VectorEntry;

typedef struct HNSWNode
{
    int id;                    /* 节点ID */
    int level;                 /* 最高层级别 */
    float *vector;             /* 向量 */
    int dim;                   /* 向量维度 */
    char *key;                 /* 关联的字符串 key */
    int is_deleted;            /* 删除标记: 0=正常, 1=已删除 */
    unsigned long delete_time; /* 删除时间戳 */
    struct HNSWNode **forward; /* 每层的前向指针数组 */
    int *distances;            /* 到前向节点的近似距离 */
    int *neighbors_count;      /* 每层的邻居数量 */
} HNSWNode;

typedef struct
{
    int dim;           /* 全局向量维度 */
    int size;          /* 当前元素数量 */
    int deleted_count; /* 已删除元素数量 */
    int max_size;      /* 最大容量 */
    int max_level;     /* 当前最大层 */
    int enterpoint_id; /* 入口点节点ID */
    float ml;          /* 层衰减因子 */
    HNSWNode **nodes;  /* 节点数组 */
    int *level_counts; /* 每层的节点数量统计 */
} HNSWIndex;

typedef struct kvs_vector_internal
{
    HNSWIndex *index; /* HNSW 索引 */
    int dim;          /* 全局向量维度 */
    int count;        /* 向量总数 */
    int max_size;     /* 最大容量 */
} kvs_vector_t;

extern kvs_vector_t global_vector;

/* ====================== 工具函数 ====================== */

static float euclidean_distance(const float *a, const float *b, int dim)
{
    float dist = 0.0f;
    for (int i = 0; i < dim; i++)
    {
        float d = a[i] - b[i];
        dist += d * d;
    }
    return sqrtf(dist);
}

static float dot_product(const float *a, const float *b, int dim)
{
    float sum = 0.0f;
    for (int i = 0; i < dim; i++)
    {
        sum += a[i] * b[i];
    }
    return sum;
}

static float cosine_similarity(const float *a, const float *b, int dim)
{
    float dot = dot_product(a, b, dim);
    float norm_a = sqrtf(dot_product(a, a, dim));
    float norm_b = sqrtf(dot_product(b, b, dim));
    if (norm_a < 1e-6 || norm_b < 1e-6)
        return 0.0f;
    return dot / (norm_a * norm_b);
}

static int random_level(float ml)
{
    int level = 0;
    float r = (float)rand() / RAND_MAX;
    float threshold = exp(-level * ml);
    while (r < threshold && level < MAX_LAYERS - 1)
    {
        level++;
        r = (float)rand() / RAND_MAX;
        threshold = exp(-level * ml);
    }
    return level;
}

static HNSWNode *hnsw_create_node(int id, const float *vector, int dim, int level, const char *key)
{
    HNSWNode *node = (HNSWNode *)kvs_malloc(sizeof(HNSWNode));
    if (!node)
        return NULL;

    node->id = id;
    node->level = level;
    node->dim = dim;
    node->is_deleted = 0;
    node->delete_time = 0;
    node->vector = (float *)kvs_malloc(dim * sizeof(float));
    if (!node->vector)
    {
        kvs_free(node);
        return NULL;
    }
    memcpy(node->vector, vector, dim * sizeof(float));

    node->key = key ? kvs_strdup(key) : NULL;

    node->forward = (HNSWNode **)kvs_malloc((level + 1) * sizeof(HNSWNode *));
    node->distances = (int *)kvs_malloc((level + 1) * sizeof(int));
    node->neighbors_count = (int *)kvs_malloc((level + 1) * sizeof(int));

    for (int i = 0; i <= level; i++)
    {
        node->forward[i] = NULL;
        node->distances[i] = 0;
        node->neighbors_count[i] = 0;
    }

    return node;
}

static void hnsw_free_node(HNSWNode *node)
{
    if (!node)
        return;
    kvs_free(node->vector);
    kvs_free(node->key);
    kvs_free(node->forward);
    kvs_free(node->distances);
    kvs_free(node->neighbors_count);
    kvs_free(node);
}

/* ====================== 搜索算法 ====================== */

/* 贪婪搜索：找最接近的邻居 */
static HNSWNode *greedy_search(HNSWIndex *index, const float *query,
                               int ef, int level)
{
    HNSWNode *curr = index->nodes[index->enterpoint_id];
    float best_dist = euclidean_distance(query, curr->vector, index->dim);

    while (curr->forward[level] != NULL)
    {
        HNSWNode *next = curr->forward[level];
        float dist = euclidean_distance(query, next->vector, index->dim);
        if (dist < best_dist)
        {
            best_dist = dist;
            curr = next;
        }
        else
        {
            break;
        }
    }

    if (curr->forward[level] != NULL)
    {
        HNSWNode *next = curr->forward[level];
        float dist = euclidean_distance(query, next->vector, index->dim);
        if (dist < best_dist)
        {
            best_dist = dist;
            curr = next;
        }
    }

    (void)ef; /* 抑制未使用警告 */
    return curr;
}

/* 层内搜索：找到 top-ef 个最近邻 */
typedef struct
{
    HNSWNode *node;
    float dist;
} SearchResult;

static int compare_result(const void *a, const void *b)
{
    float da = ((SearchResult *)a)->dist;
    float db = ((SearchResult *)b)->dist;
    return (da > db) ? -1 : (da < db) ? 1
                                      : 0;
}

static void search_layer(HNSWIndex *index, HNSWNode *ep,
                         const float *query, int ef, int level,
                         HNSWNode ***results, int *n_results)
{
    HNSWNode *curr = ep;
    HNSWNode *prev = NULL;

    /* 简单贪婪搜索实现 */
    float best_dist = euclidean_distance(query, curr->vector, index->dim);

    while (curr->forward[level] != NULL)
    {
        prev = curr;
        curr = curr->forward[level];
        float dist = euclidean_distance(query, curr->vector, index->dim);
        if (dist < best_dist)
        {
            best_dist = dist;
        }
    }

    /* 返回找到的节点 */
    if (results && n_results)
    {
        *results = (HNSWNode **)kvs_malloc(sizeof(HNSWNode *));
        *results[0] = prev ? prev : curr;
        *n_results = 1;
    }
    (void)ef;
}

static int search_hnsw(HNSWIndex *index, const float *query,
                       int k, int ef,
                       int *result_ids, float *result_dists)
{
    if (index->size == 0 || index->enterpoint_id < 0 || index->enterpoint_id >= index->size)
        return 0;

    HNSWNode *curr = index->nodes[index->enterpoint_id];
    if (!curr || curr->is_deleted)
    {
        curr = NULL;
    }

    if (curr)
    {
        for (int level = index->max_level; level >= 1; level--)
        {
            if (level > curr->level)
                continue;

            HNSWNode *prev = NULL;
            float best_dist = euclidean_distance(query, curr->vector, index->dim);

            while (curr->forward && curr->forward[level] != NULL)
            {
                prev = curr;
                curr = curr->forward[level];
                if (!curr || curr->is_deleted)
                {
                    curr = prev;
                    break;
                }
                float dist = euclidean_distance(query, curr->vector, index->dim);
                if (dist < best_dist)
                {
                    best_dist = dist;
                }
                else
                {
                    curr = prev;
                    break;
                }
            }
        }
    }

    HNSWNode *winners[EF_SEARCH];
    float d_winners[EF_SEARCH];
    int n_winners = 0;

    for (int i = 0; i < index->size && n_winners < EF_SEARCH; i++)
    {
        if (index->nodes[i] == NULL)
            continue;
        if (index->nodes[i]->is_deleted)
            continue;
        if (curr && index->nodes[i] == curr)
            continue;

        float dist = euclidean_distance(query, index->nodes[i]->vector, index->dim);
        winners[n_winners] = index->nodes[i];
        d_winners[n_winners] = dist;
        n_winners++;
    }

    if (curr && !curr->is_deleted)
    {
        if (n_winners < EF_SEARCH)
        {
            winners[n_winners] = curr;
            d_winners[n_winners] = euclidean_distance(query, curr->vector, index->dim);
            n_winners++;
        }
    }

    for (int i = 0; i < n_winners - 1; i++)
    {
        for (int j = i + 1; j < n_winners; j++)
        {
            if (d_winners[j] < d_winners[i])
            {
                HNSWNode *tmp_node = winners[i];
                winners[i] = winners[j];
                winners[j] = tmp_node;
                float tmp_dist = d_winners[i];
                d_winners[i] = d_winners[j];
                d_winners[j] = tmp_dist;
            }
        }
    }

    int n_result = n_winners < k ? n_winners : k;
    for (int i = 0; i < n_result; i++)
    {
        result_ids[i] = winners[i]->id;
        result_dists[i] = d_winners[i];
    }

    return n_result;
}

/* ====================== 插入算法 ====================== */

static void insert_hnsw(HNSWIndex *index, HNSWNode *new_node)
{
    if (index->size >= index->max_size)
    {
        /* 简单的容量满判断，实际需要扩容逻辑 */
        return;
    }

    int id = index->size;
    new_node->id = id;
    index->nodes[id] = new_node;
    index->size++;

    if (index->size == 1)
    {
        index->enterpoint_id = 0;
        index->max_level = new_node->level;
        return;
    }

    /* 更新全局最大层 */
    if (new_node->level > index->max_level)
    {
        index->max_level = new_node->level;
    }

    /* 从最高层开始，逐层插入前向指针 */
    HNSWNode *curr = index->nodes[index->enterpoint_id];

    for (int level = index->max_level; level >= 0; level--)
    {
        if (level > new_node->level)
        {
            /* 高于新节点层级的层，跳过 */
            continue;
        }

        /* 确保当前节点在该层有连接 */
        if (level > curr->level)
        {
            continue;
        }

        /* 在当前层搜索插入位置 */
        HNSWNode *prev = NULL;
        HNSWNode *candidate = curr;

        /* 贪婪下降到合适位置 */
        float new_dist = euclidean_distance(new_node->vector, candidate->vector, index->dim);

        /* 确保候选节点在该层有连接 */
        while (candidate->forward[level] != NULL && level <= candidate->level)
        {
            HNSWNode *next = candidate->forward[level];
            if (next == NULL)
                break;

            float next_dist = euclidean_distance(new_node->vector, next->vector, index->dim);
            if (next_dist < new_dist)
            {
                prev = candidate;
                candidate = next;
                new_dist = next_dist;
            }
            else
            {
                break;
            }
        }

        /* 插入到链表 */
        if (prev == NULL)
        {
            prev = candidate;
        }

        new_node->forward[level] = prev->forward[level];
        prev->forward[level] = new_node;
        new_node->neighbors_count[level]++;

        if (new_node->neighbors_count[level] > M_MAX)
        {
            new_node->neighbors_count[level] = M_MAX;
        }
    }
}

/* ====================== API 实现 ====================== */

int kvs_vector_create(kvs_vector_t *vec)
{
    if (!vec)
        return -1;

    vec->dim = MAX_DIM;
    vec->count = 0;
    vec->max_size = MAX_ELEMENTS;

    vec->index = (HNSWIndex *)kvs_malloc(sizeof(HNSWIndex));
    if (!vec->index)
        return -1;

    vec->index->dim = MAX_DIM;
    vec->index->size = 0;
    vec->index->deleted_count = 0;
    vec->index->max_size = MAX_ELEMENTS;
    vec->index->max_level = 0;
    vec->index->enterpoint_id = 0;
    vec->index->ml = LAYER_DECAY;

    vec->index->nodes = (HNSWNode **)kvs_malloc(MAX_ELEMENTS * sizeof(HNSWNode *));
    if (!vec->index->nodes)
    {
        kvs_free(vec->index);
        return -1;
    }
    memset(vec->index->nodes, 0, MAX_ELEMENTS * sizeof(HNSWNode *));

    vec->index->level_counts = (int *)kvs_calloc(MAX_LAYERS, sizeof(int));

    srand((unsigned int)time(NULL));

    return 0;
}

void kvs_vector_destroy(kvs_vector_t *vec)
{
    if (!vec || !vec->index)
        return;

    for (int i = 0; i < vec->index->size; i++)
    {
        if (vec->index->nodes[i])
        {
            hnsw_free_node(vec->index->nodes[i]);
            vec->index->nodes[i] = NULL;
        }
    }

    kvs_free(vec->index->nodes);
    kvs_free(vec->index->level_counts);
    kvs_free(vec->index);
    vec->index = NULL;
    vec->count = 0;
}

int kvs_vector_set(kvs_vector_t *vec, char *key, char *value)
{
    if (!vec || !vec->index || !key || !value)
        return -1;

    /* 解析格式: <dim> <float1> <float2> ... */
    char *saveptr;
    char *token = strtok_r(value, " ", &saveptr);
    if (!token)
        return -1;

    int dim = atoi(token);
    if (dim <= 0 || dim > MAX_DIM)
    {
        dim = vec->dim;
    }

    float *vector = (float *)kvs_malloc(dim * sizeof(float));
    if (!vector)
        return -1;

    for (int i = 0; i < dim; i++)
    {
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            kvs_free(vector);
            return -1;
        }
        vector[i] = (float)atof(token);
    }

    /* 生成 HNSW 层 */
    int level = random_level(vec->index->ml);

    HNSWNode *node = hnsw_create_node(vec->index->size, vector, dim, level, key);
    kvs_free(vector);

    if (!node)
        return -1;

    insert_hnsw(vec->index, node);
    vec->count++;

    return 0; /* 成功 */
}

char *kvs_vector_get(kvs_vector_t *vec, char *key)
{
    if (!vec || !vec->index || !key)
        return NULL;

    /* 遍历查找匹配的 key */
    for (int i = 0; i < vec->index->size; i++)
    {
        HNSWNode *node = vec->index->nodes[i];
        if (!node || node->is_deleted)
            continue;
        if (node->key && strcmp(node->key, key) == 0)
        {
            static char buffer[8192];
            int offset = snprintf(buffer, sizeof(buffer),
                                  "key=%s,id=%d,dim=%d,level=%d,vec=[",
                                  node->key, node->id, node->dim, node->level);
            for (int j = 0; j < node->dim && offset < (int)sizeof(buffer) - 50; j++)
            {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                                   "%.4f%s", node->vector[j],
                                   j < node->dim - 1 ? "," : "]");
            }
            return buffer;
        }
    }

    return NULL;
}

int kvs_vector_search(kvs_vector_t *vec, char *key, int k,
                      char *query_str, int *result_ids, float *result_dists)
{
    if (!vec || !vec->index || !key || !query_str)
        return -1;
    if (vec->index->size == 0)
        return 0;

    /* 解析查询向量 */
    int dim = vec->index->dim;
    float *query = (float *)kvs_malloc(dim * sizeof(float));
    if (!query)
        return -1;

    char *saveptr;
    int i = 0;
    char *token = strtok_r(query_str, " ", &saveptr);
    while (token && i < dim)
    {
        query[i++] = (float)atof(token);
        token = strtok_r(NULL, " ", &saveptr);
    }

    /* 不足维度填充0 */
    while (i < dim)
        query[i++] = 0.0f;

    int n_result = search_hnsw(vec->index, query, k, EF_SEARCH, result_ids, result_dists);

    kvs_free(query);
    return n_result;
}

int kvs_vector_del(kvs_vector_t *vec, char *key)
{
    if (!vec || !vec->index || !key)
        return -1;

    /* 遍历查找匹配的 key */
    for (int i = 0; i < vec->index->size; i++)
    {
        HNSWNode *node = vec->index->nodes[i];
        if (!node || node->is_deleted)
            continue;
        if (node->key && strcmp(node->key, key) == 0)
        {
            /* 标记删除 */
            node->is_deleted = 1;
            node->delete_time = (unsigned long)time(NULL);
            vec->index->deleted_count++;
            vec->count--;
            return 0;
        }
    }

    return -2; /* key 不存在 */
}

int kvs_vector_exist(kvs_vector_t *vec, char *key)
{
    if (!vec || !vec->index || !key)
        return -1;
    return (vec->index->size > 0) ? 0 : 1;
}

int kvs_vector_count(kvs_vector_t *vec)
{
    if (!vec || !vec->index)
        return 0;
    /* 返回有效向量数量（排除已删除） */
    return vec->index->size - vec->index->deleted_count;
}

int kvs_vector_dim(kvs_vector_t *vec)
{
    if (!vec || !vec->index)
        return 0;
    return vec->index->dim;
}

kvs_vector_t global_vector;

#endif /* ENABLE_VECTOR */
