#include "kvstore.h"

// singleton
kvs_array_t global_array = {0};

int kvs_array_create(kvs_array_t *inst)
{

    if (!inst)
        return -1;
    if (inst->table)
    {
        printf("table has alloc\n");
        return -1;
    }
    inst->table = kvs_calloc(KVS_ARRAY_SIZE, sizeof(kvs_array_item_t));
    if (!inst->table)
    {
        printf("table alloc failed\n");
        return -1;
    }

    inst->idx = 0;   /* next append position (high-water mark) */
    inst->total = 0; /* actual live entries */

    return 0;
}

void kvs_array_destroy(kvs_array_t *inst)
{

    if (!inst)
        return;

    if (inst->table)
    {
        for (int i = 0; i < inst->idx; i++)
        {
            kvs_free(inst->table[i].key);
            kvs_free(inst->table[i].value);
        }
        kvs_free(inst->table);
        inst->table = NULL;
    }
    inst->idx = 0;
    inst->total = 0;
}

/*
 * @return <0 error;  =0, success;  >0, exist
 * @brief set key-value pair
 */
int kvs_array_set(kvs_array_t *inst, char *key, char *value)
{

    if (inst == NULL || key == NULL || value == NULL)
        return -1;
    if (inst->total >= KVS_ARRAY_SIZE)
        return -1;

    /* 检查 key 是否已存在 */
    char *str = kvs_array_get(inst, key);
    if (str)
    {
        return 1;
    }

    char *kcopy = kvs_strdup(key);
    if (kcopy == NULL)
        return -2;

    char *kvalue = kvs_strdup(value);
    if (kvalue == NULL)
    {
        kvs_free(kcopy);
        return -2;
    }

    /* 先尝试复用已删除的空槽 */
    for (int i = 0; i < inst->idx; i++)
    {
        if (inst->table[i].key == NULL)
        {
            inst->table[i].key = kcopy;
            inst->table[i].value = kvalue;
            inst->total++;
            return 0;
        }
    }

    /* 没有空槽，追加到末尾 */
    if (inst->idx < KVS_ARRAY_SIZE)
    {
        inst->table[inst->idx].key = kcopy;
        inst->table[inst->idx].value = kvalue;
        inst->idx++;
        inst->total++;
        return 0;
    }

    kvs_free(kcopy);
    kvs_free(kvalue);
    return -1;
}

char *kvs_array_get(kvs_array_t *inst, char *key)
{

    if (inst == NULL || key == NULL)
        return NULL;

    for (int i = 0; i < inst->idx; i++)
    {
        if (inst->table[i].key == NULL)
        {
            continue;
        }
        if (strcmp(inst->table[i].key, key) == 0)
        {
            return inst->table[i].value;
        }
    }

    return NULL;
}

/*
 * @return <0, error;   =0, success;    >0, no exist
 * @brief delete key-value pair
 */
int kvs_array_del(kvs_array_t *inst, char *key)
{

    if (inst == NULL || key == NULL)
        return -1;

    for (int i = 0; i < inst->idx; i++)
    {
        if (inst->table[i].key && strcmp(inst->table[i].key, key) == 0)
        {

            kvs_free(inst->table[i].key);
            inst->table[i].key = NULL;

            kvs_free(inst->table[i].value);
            inst->table[i].value = NULL;

            inst->total--;

            /* 收缩 idx 如果删除的是末尾连续空槽 */
            while (inst->idx > 0 && inst->table[inst->idx - 1].key == NULL)
            {
                inst->idx--;
            }
            return 0;
        }
    }
    return 1; /* no exist */
}

/*
 * @return: <0, error;  ==0, success;  >0, no exist
 */

int kvs_array_mod(kvs_array_t *inst, char *key, char *value)
{

    if (inst == NULL || key == NULL || value == NULL)
        return -1;

    for (int i = 0; i < inst->idx; i++)
    {
        if (inst->table[i].key == NULL)
        {
            continue;
        }
        if (strcmp(inst->table[i].key, key) == 0)
        {

            kvs_free(inst->table[i].value);

            inst->table[i].value = kvs_strdup(value);
            if (inst->table[i].value == NULL)
                return -2;

            return 0;
        }
    }

    return 1; /* no exist */
}

/**
 * @return 0: exit;  1: no exist
 * @brief check if key exist
 */

int kvs_array_exist(kvs_array_t *inst, char *key)
{

    if (!inst || !key)
        return -1;

    char *str = kvs_array_get(inst, key);
    if (!str)
    {
        return 1;
    }
    return 0;
}