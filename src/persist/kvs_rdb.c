#include "kvstore.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <errno.h>

#define RDB_MAGIC "KVSDB"
#define RDB_VERSION 1
#define RDB_FILENAME "dump.rdb"

#define ENGINE_ARRAY 0
#define ENGINE_HASH 1
#define ENGINE_RBTREE 2
#define ENGINE_SKIPLIST 3

static int rdb_write_int(int fd, int value)
{
    int net_value = htonl(value);
    return write(fd, &net_value, sizeof(int));
}

static int rdb_read_int(int fd, int *value)
{
    int net_value;
    if (read(fd, &net_value, sizeof(int)) != sizeof(int))
        return -1;
    *value = ntohl(net_value);
    return 0;
}

static int rdb_write_string(int fd, const char *str)
{
    int len = strlen(str);
    if (rdb_write_int(fd, len) < 0)
        return -1;
    return write(fd, str, len);
}

static int rdb_read_string(int fd, char **str)
{
    int len;
    if (rdb_read_int(fd, &len) < 0)
        return -1;
    if (len <= 0 || len > 1024 * 1024)
        return -1;
    
    *str = (char *)kvs_malloc(len + 1);
    if (*str == NULL)
        return -1;
    
    if (read(fd, *str, len) != len)
    {
        kvs_free(*str);
        return -1;
    }
    (*str)[len] = '\0';
    return 0;
}

static int rdb_save_header(int fd)
{
    if (write(fd, RDB_MAGIC, 5) != 5)
        return -1;
    
    if (rdb_write_int(fd, RDB_VERSION) < 0)
        return -1;
    
    long long timestamp = (long long)time(NULL);
    int high = htonl((int)(timestamp >> 32));
    int low = htonl((int)(timestamp & 0xFFFFFFFF));
    write(fd, &high, sizeof(int));
    write(fd, &low, sizeof(int));
    
    return 0;
}

static int rdb_load_header(int fd, int *version, long long *timestamp)
{
    char magic[5];
    if (read(fd, magic, 5) != 5)
        return -1;
    
    if (memcmp(magic, RDB_MAGIC, 5) != 0)
        return -1;
    
    if (rdb_read_int(fd, version) < 0)
        return -1;
    
    if (*version != RDB_VERSION)
        return -1;
    
    int high, low;
    if (read(fd, &high, sizeof(int)) != sizeof(int) ||
        read(fd, &low, sizeof(int)) != sizeof(int))
        return -1;
    
    *timestamp = ((long long)ntohl(high) << 32) | (ntohl(low) & 0xFFFFFFFF);
    return 0;
}

extern kvs_array_t global_array;
extern kvs_rbtree_t global_rbtree;
extern kvs_hash_t global_hash;
extern kvs_skip_t global_skip;

int rdb_save(const char *filename)
{
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        printf("RDB: Failed to open file %s: %s\n", filename, strerror(errno));
        return -1;
    }
    
    printf("RDB: Saving to %s...\n", filename);
    
    if (rdb_save_header(fd) < 0)
    {
        close(fd);
        return -1;
    }
    
    int count = 0;
    
#if ENABLE_ARRAY
    for (int i = 0; i < global_array.idx; i++)
    {
        if (global_array.table[i].key && global_array.table[i].value)
        {
            rdb_write_int(fd, ENGINE_ARRAY);
            rdb_write_string(fd, global_array.table[i].key);
            rdb_write_string(fd, global_array.table[i].value);
            count++;
        }
    }
#endif
    
#if ENABLE_HASH
    for (int i = 0; i < global_hash.max_slots; i++)
    {
        hashnode_t *node = global_hash.nodes[i];
        while (node)
        {
            rdb_write_int(fd, ENGINE_HASH);
            rdb_write_string(fd, node->key);
            rdb_write_string(fd, node->value);
            node = node->next;
            count++;
        }
    }
#endif
    
#if ENABLE_RBTREE
    int save_rbtree_node(int fd, rbtree_node *node, rbtree_node *nil);
    count += save_rbtree_node(fd, global_rbtree.root, global_rbtree.nil);
#endif
    
#if ENABLE_SKIP
    Node *x = global_skip.header;
    while (x->forward[0] != NULL)
    {
        x = x->forward[0];
        rdb_write_int(fd, ENGINE_SKIPLIST);
        rdb_write_string(fd, x->key);
        rdb_write_string(fd, x->value);
        count++;
    }
#endif
    
    rdb_write_int(fd, -1);
    
    close(fd);
    printf("RDB: Saved %d keys to %s\n", count, filename);
    return count;
}

#if ENABLE_RBTREE
int save_rbtree_node(int fd, rbtree_node *node, rbtree_node *nil)
{
    if (node == nil)
        return 0;
    
    int count = 0;
    count += save_rbtree_node(fd, node->left, nil);
    
    if (node->key)
    {
        rdb_write_int(fd, ENGINE_RBTREE);
        rdb_write_string(fd, node->key);
        rdb_write_string(fd, (char *)node->value);
        count++;
    }
    
    count += save_rbtree_node(fd, node->right, nil);
    return count;
}
#endif

int rdb_load(const char *filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        printf("RDB: No existing dump file found (%s)\n", filename);
        return 0;
    }
    
    printf("RDB: Loading from %s...\n", filename);
    
    int version;
    long long timestamp;
    if (rdb_load_header(fd, &version, &timestamp) < 0)
    {
        printf("RDB: Invalid file format\n");
        close(fd);
        return -1;
    }
    
    printf("RDB: File version %d, timestamp %lld\n", version, timestamp);
    
    int count = 0;
    while (1)
    {
        int engine;
        if (rdb_read_int(fd, &engine) < 0)
            break;
        
        if (engine == -1)
            break;
        
        char *key = NULL, *value = NULL;
        if (rdb_read_string(fd, &key) < 0 || rdb_read_string(fd, &value) < 0)
        {
            if (key) kvs_free(key);
            if (value) kvs_free(value);
            break;
        }
        
        switch (engine)
        {
#if ENABLE_ARRAY
        case ENGINE_ARRAY:
            kvs_array_set(&global_array, key, value);
            break;
#endif
#if ENABLE_HASH
        case ENGINE_HASH:
            kvs_hash_set(&global_hash, key, value);
            break;
#endif
#if ENABLE_RBTREE
        case ENGINE_RBTREE:
            kvs_rbtree_set(&global_rbtree, key, value);
            break;
#endif
#if ENABLE_SKIP
        case ENGINE_SKIPLIST:
            kvs_skip_set(&global_skip, key, value);
            break;
#endif
        }
        
        kvs_free(key);
        kvs_free(value);
        count++;
    }
    
    close(fd);
    printf("RDB: Loaded %d keys from %s\n", count, filename);
    return count;
}

int rdb_save_default(void)
{
    return rdb_save(RDB_FILENAME);
}

int rdb_load_default(void)
{
    return rdb_load(RDB_FILENAME);
}
