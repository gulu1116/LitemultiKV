


#ifndef __KV_STORE_H__
#define __KV_STORE_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>

#define NETWORK_REACTOR     0
#define NETWORK_PROACTOR    1
#define NETWORK_NTYCO       2

#define NETWORK_SELECT      NETWORK_NTYCO


#define KVS_MAX_TOKENS      128

#define ENABLE_ARRAY        1
#define ENABLE_RBTREE       1
#define ENABLE_HASH			1
#define ENABLE_SKIP         1

#define ENABLE_KEY_CHAR	1

#if ENABLE_KEY_CHAR
typedef char* KEY_TYPE;
#else
typedef int KEY_TYPE;
#endif


typedef int (*msg_handler)(char *msg, int length, char *response);


extern int reactor_start(unsigned short port, msg_handler handler);
extern int ntyco_start(unsigned short port, msg_handler handler);
extern int proactor_start(unsigned short port, msg_handler handler);



#if ENABLE_ARRAY

typedef struct kvs_array_item_s {
    char *key;
    char *value;
}kvs_array_item_t;

#define KVS_ARRAY_SIZE      12000

typedef struct kvs_array_s {
    kvs_array_item_t *table;
    int idx;
    int total;
} kvs_array_t;

int kvs_array_create(kvs_array_t *inst);
void kvs_array_destroy(kvs_array_t *inst);

int kvs_array_set(kvs_array_t *inst, char *key, char *value);
char* kvs_array_get(kvs_array_t *inst, char *key);
int kvs_array_del(kvs_array_t *inst, char *key);
int kvs_array_mod(kvs_array_t *inst, char *key, char *value);
int kvs_array_exist(kvs_array_t *inst, char *key);

#endif

#if ENABLE_RBTREE


#define RED				1
#define BLACK 			2

typedef struct _rbtree_node {
	unsigned char color;
	struct _rbtree_node *right;
	struct _rbtree_node *left;
	struct _rbtree_node *parent;
	KEY_TYPE key;
	void *value;
} rbtree_node;

typedef struct _rbtree {
	rbtree_node *root;
	rbtree_node *nil;
} rbtree;



typedef struct _rbtree kvs_rbtree_t;

// 5 + 2
int kvs_rbtree_create(kvs_rbtree_t *inst);
void kvs_rbtree_destroy(kvs_rbtree_t *inst);
int kvs_rbtree_set(kvs_rbtree_t *inst, char *key, char *value);
char* kvs_rbtree_get(kvs_rbtree_t *inst, char *key);
int kvs_rbtree_del(kvs_rbtree_t *inst, char *key);
int kvs_rbtree_mod(kvs_rbtree_t *inst, char *key, char *value);
int kvs_rbtree_exist(kvs_rbtree_t *inst, char *key);

#endif

#if ENABLE_HASH

#define MAX_KEY_LEN		128
#define MAX_VALUE_LEN	512
#define MAX_TABLE_SIZE	1024

// 1: key and value are char * (string pointers) and dynamically allocate memory
// 0: uses a fixed-length array char key[MAX_KEY_LEN]
#define ENABLE_KEY_POINTER	1


typedef struct hashnode_s {

#if ENABLE_KEY_POINTER
	char *key;
	char *value;
#else
	char key[MAX_KEY_LEN];
	char value[MAX_VALUE_LEN];
#endif
	struct hashnode_s *next;
	
} hashnode_t;


typedef struct hashtable_s {

	hashnode_t **nodes; //* change **, 

	int max_slots;
	int count;

} hashtable_t;

typedef struct hashtable_s kvs_hash_t;

int kvs_hash_create(kvs_hash_t *hash);
void kvs_hash_destroy(kvs_hash_t *hash);
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value);
char* kvs_hash_get(kvs_hash_t *hash, char *key);
int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value);
int kvs_hash_del(kvs_hash_t *hash, char *key);
int kvs_hash_exist(kvs_hash_t *hash, char *key);


#endif


#if ENABLE_SKIP

#define MAX_LEVEL 6

typedef struct Node {
    char *key;
    char *value;
    struct Node** forward;
} Node;

typedef struct SkipList {
    int level;
    Node* header;
} SkipList;

typedef struct SkipList kvs_skip_t;

int kvs_skip_create(kvs_skip_t *hash);
void kvs_skip_destroy(kvs_skip_t *hash);
int kvs_skip_set(kvs_skip_t *hash, char *key, char *value);
char* kvs_skip_get(kvs_skip_t *hash, char *key);
int kvs_skip_mod(kvs_skip_t *hash, char *key, char *value);
int kvs_skip_del(kvs_skip_t *hash, char *key);
int kvs_skip_exist(kvs_skip_t *hash, char *key);

#endif


void *kvs_malloc(size_t size);
void kvs_free(void *ptr);


#endif