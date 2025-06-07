


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "kvstore.h"


// key, value ---> the size of array is fixed
// we need add function ---> modify 


kvs_hash_t global_hash;


//Connection 
// 'C' + 'o' + 'n'
static int _hash(char *key, int size) {

	if (!key) return -1;

	int sum = 0;
	int i = 0;

	while (key[i] != 0) {
		sum += key[i];
		i ++;
	}

	return sum % size;

}

hashnode_t *_create_node(char *key, char *value) {

	hashnode_t *node = (hashnode_t*)kvs_malloc(sizeof(hashnode_t));
	if (!node) return NULL;

// 动态分配模式
#if ENABLE_KEY_POINTER

	char *kcopy = kvs_malloc(strlen(key) + 1); // +1 用于 '\0' 结束符
    if (kcopy == NULL) {
        kvs_free(node); // 分配失败，清理已分配内存
        return NULL;
    }
    memset(kcopy, 0, strlen(key) + 1);
    strncpy(kcopy, key, strlen(key));

	node->key = kcopy; // 指向新分配的内存

	char *kvalue = kvs_malloc(strlen(value) + 1);
    if (kvalue == NULL) {
		kvs_free(kcopy);
		kvs_free(kvalue);
		return NULL; // 内存分配失败
	}
    memset(kvalue, 0, strlen(value) + 1);
    strncpy(kvalue, value, strlen(value));

	node->value = kvalue;

#else
	// 固定大小模式：直接复制到结构体数组
	strncpy(node->key, key, MAX_KEY_LEN);
	strncpy(node->value, value, MAX_VALUE_LEN);
#endif
	node->next = NULL;

	return node;
}


//
int kvs_hash_create(kvs_hash_t *hash) {

	if (!hash) return -1;

	hash->nodes = (hashnode_t**)kvs_malloc(sizeof(hashnode_t*) * MAX_TABLE_SIZE);
	if (!hash->nodes) return -1;

	hash->max_slots = MAX_TABLE_SIZE;
	hash->count = 0; 

	return 0;
}

// 
void kvs_hash_destroy(kvs_hash_t *hash) {

	if (!hash) return;

	int i = 0;
	for (i = 0; i < hash->max_slots; i ++) {
		hashnode_t *node = hash->nodes[i];

		while (node != NULL) { // error

			hashnode_t *tmp = node;
			node = node->next;
			hash->nodes[i] = node;

// 动态模式：额外释放键值内存
#if ENABLE_KEY_POINTER
            kvs_free(tmp->key);
            kvs_free(tmp->value);
#endif
			kvs_free(tmp); // 释放节点本身
			
		}
	}

	kvs_free(hash->nodes); // 释放桶数组
	
}


// 5 + 2

// mp
int kvs_hash_set(kvs_hash_t *hash, char *key, char *value) {

	if (!hash || !key || !value) return -1;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *node = hash->nodes[idx];
#if 1 // 检查键是否已存在
	while (node != NULL) {
		if (strcmp(node->key, key) == 0) { 
			return 1;
		}
		node = node->next;
	}
#endif

	hashnode_t *new_node = _create_node(key, value);
	new_node->next = hash->nodes[idx];
	hash->nodes[idx] = new_node;
	
	hash->count ++;

	return 0;
}


char* kvs_hash_get(kvs_hash_t *hash, char *key) {

	if (!hash || !key) return NULL;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			return node->value;
		}

		node = node->next;
	}

	return NULL;
}

int kvs_hash_mod(kvs_hash_t *hash, char *key, char *value) {

	if (!hash || !key) return -1;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			break;
		}

		node = node->next;
	}

	if (node == NULL) {
		return 1; // no exist
	}

// 动态模式：释放旧值，分配新值
#if ENABLE_KEY_POINTER
	kvs_free(node->value);

	char *kvalue = kvs_malloc(strlen(value) + 1);
	if (kvalue == NULL) return -2; // 内存分配失败
	memset(kvalue, 0, strlen(value) + 1);
	strncpy(kvalue, value, strlen(value));

	node->value = kvalue;

#else
// 固定模式：直接覆盖
    strncpy(node->value, value, MAX_VALUE_LEN);
#endif

	return 0;
}


int kvs_hash_count(kvs_hash_t *hash) {
	return hash->count;
}

int kvs_hash_del(kvs_hash_t *hash, char *key) {
	if (!hash || !key) return -2;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hashnode_t *head = hash->nodes[idx];
	if (head == NULL) return 1; // no exist

	// delete head node
	if (strcmp(head->key, key) == 0) {
		hashnode_t *tmp = head->next;
		hash->nodes[idx] = tmp;
		
// 释放资源
#if ENABLE_KEY_POINTER
        kvs_free(head->key);
        kvs_free(head->value);
#endif

		kvs_free(head);
		hash->count --;
		
		return 0;
	}

	// delete middle/rear node
	hashnode_t *cur = head;
	while (cur->next != NULL) {
		if (strcmp(cur->next->key, key) == 0) break; // search node
		
		cur = cur->next;
	}

	if (cur->next == NULL) {
		
		return 1;
	}

	hashnode_t *tmp = cur->next;
	cur->next = tmp->next;

#if ENABLE_KEY_POINTER
	kvs_free(tmp->key);
	kvs_free(tmp->value);
#endif
	kvs_free(tmp);

	hash->count --;
	
	return 0;
}


int kvs_hash_exist(kvs_hash_t *hash, char *key) {

	char *value = kvs_hash_get(hash, key);
	if (!value) return 1;
	
	else return 0;
	
}

#if 0
int main() {

	kvs_hash_create(&hash);

	kvs_hash_set(&hash, "Student1", "GuLu");
	kvs_hash_set(&hash, "Student2", "Lighting");
	kvs_hash_set(&hash, "Student3", "Lian");
	kvs_hash_set(&hash, "Student4", "Yao");
	kvs_hash_set(&hash, "Student5", "Lin");

	char *value1 = kvs_hash_get(&hash, "Student1");
	printf("Student1: %s\n", value1);

	char *value2 = kvs_hash_get(&hash, "Student2");
	printf("Student2: %s\n", value2);

	char *value3 = kvs_hash_get(&hash, "Student3");
	printf("Student3: %s\n", value3);

	int ret = kvs_hash_mod(&hash, "Student3", "Zhen");
	printf("Mod Student3 to %s\n", value3);

	ret = kvs_hash_del(&hash, "Student1");
	printf("Delete Student1 ret : %d\n", ret);

	ret = kvs_hash_exist(&hash, "Student1");
	printf("Exist Student1 ret : %d\n", ret);

	kvs_hash_destory(&hash);

	return 0;
}

#endif