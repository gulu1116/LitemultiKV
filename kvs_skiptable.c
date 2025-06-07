


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "kvstore.h"

kvs_skip_t global_skip;


Node* createNode(int level, char *key, char *value) {
    if (!key || !value || level < 0) return NULL;

    Node* newNode = (Node*)kvs_malloc(sizeof(Node));
    if (!newNode) return NULL;

    // 分配并拷贝 key
    newNode->key = (char*)kvs_malloc(strlen(key) + 1);
    if (!newNode->key) {
        kvs_free(newNode);
        return NULL;
    }
    strcpy(newNode->key, key);  // 安全地复制字符串（因为我们已分配足够内存）

    // 分配并拷贝 value
    newNode->value = (char*)kvs_malloc(strlen(value) + 1);
    if (!newNode->value) {
        kvs_free(newNode->key);
        kvs_free(newNode);
        return NULL;
    }
    strcpy(newNode->value, value);

    // 分配 forward 数组
    newNode->forward = (Node**)kvs_malloc((level + 1) * sizeof(Node*));
    if (!newNode->forward) {
        kvs_free(newNode->value);
        kvs_free(newNode->key);
        kvs_free(newNode);
        return NULL;
    }

    // 初始化 forward 数组
    for (int i = 0; i <= level; ++i) {
        newNode->forward[i] = NULL;
    }

    return newNode;
}

void freeNode(Node* node) {
    if (!node) return;
    kvs_free(node->key);
    kvs_free(node->value);
    kvs_free(node->forward);
    kvs_free(node);
}



// 随机层级生成（保持原样，不需修改）
int randomLevel() {
   int level = 0;
   while (rand() < RAND_MAX / 2 && level < MAX_LEVEL)
      level++;
   return level;
}


int kvs_skip_create(kvs_skip_t *skipList) {
    if (!skipList) return -1;  // 检查指针有效性
    
    skipList->level = 0;  // 初始化跳表层级
    
    // 创建头节点（键值设为无效值-1）
    skipList->header = createNode(MAX_LEVEL, "", "");
    if (!skipList->header) return -1;  // 节点创建失败处理
    
    // 初始化所有层级的前向指针
    for (int i = 0; i <= MAX_LEVEL; ++i) {
        skipList->header->forward[i] = NULL;
    }
    
    return 0;  // 返回成功状态
}

void kvs_skip_destroy(kvs_skip_t *skipList) {
    if (!skipList || !skipList->header) return;

    Node *current = skipList->header->forward[0];  // 从第 0 层开始遍历所有节点
    while (current) {
        Node *next = current->forward[0];  // 保存下一个节点
        freeNode(current);                 // 释放当前节点
        current = next;                    // 移动到下一个节点
    }

    freeNode(skipList->header);  // 最后释放头节点
    skipList->header = NULL;
    skipList->level = 0;
}



int kvs_skip_set(kvs_skip_t *skipList, char *key, char *value) {

    if (!skipList || !skipList->header || !key || !value) return -1;

    Node* update[MAX_LEVEL + 1];
    Node* current = skipList->header;

    for (int i = skipList->level; i >= 0; --i) {
            while (current->forward[i] != NULL && strcmp(current->forward[i]->key, key) < 0)
                current = current->forward[i];
            update[i] = current;
    }

   current = current->forward[0];

    // 如果 key 已存在，返回 1
    if (current != NULL && strcmp(current->key, key) == 0) {
        return 1;
    }

    int level = randomLevel();

    if (level > skipList->level) {
        for (int i = skipList->level + 1; i <= level; ++i)
            update[i] = skipList->header;
        skipList->level = level;
    }

    Node* newNode = createNode(level, key, value);
    if (!newNode) return -1;

    for (int i = 0; i <= level; ++i) {
        newNode->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = newNode;
    }
      
    return 0;  // 插入成功
}

/*
void display(SkipList* skipList) {
    printf("Skip List:\n");
    
    for (int i = 0; i <= skipList->level; ++i) {
        Node* node = skipList->header->forward[i];
        printf("Level %d: ", i);
        
        while (node != NULL) {
            printf("%d ", node->key);
            node = node->forward[i];
        }
        
        printf("\n");
    }
}
*/

char* kvs_skip_get(kvs_skip_t *skipList, char *key) {

    if (!skipList || !key) return NULL;

    Node* current = skipList->header;

    for (int i = skipList->level; i >= 0; --i) {
        while (current->forward[i] != NULL && strcmp(current->forward[i]->key, key) < 0)
            current = current->forward[i];
    }

    current = current -> forward[0];

    if(current && strcmp(current->key, key) == 0){
        return current->value;
    }else{
        return NULL; // 未找到
    }
}

int kvs_skip_mod(kvs_skip_t *skipList, char *key, char *value) {
    if (!skipList || !key || !value) return -1;

    Node *current = skipList->header;

    // 从顶层向下搜索 key
    for (int i = skipList->level; i >= 0; --i) {
        while (current->forward[i] != NULL &&
               strcmp(current->forward[i]->key, key) < 0) {
            current = current->forward[i];
        }
    }

    current = current->forward[0];

    // 如果 key 存在
    if (current && strcmp(current->key, key) == 0) {
        // 释放旧值
        kvs_free(current->value);

        // 分配新值
        char *new_value = kvs_malloc(strlen(value) + 1);
        if (!new_value) return -2; // 内存分配失败

        memset(new_value, 0, strlen(value) + 1);
        strncpy(new_value, value, strlen(value));

        current->value = new_value;

        return 0; // 修改成功
    }

    return 1; // key 不存在
}



int kvs_skip_del(kvs_skip_t *skipList, char *key) {
    if (!skipList || !key) return -1;

    Node *update[MAX_LEVEL + 1];
    Node *current = skipList->header;

    // 查找每一层中刚好小于 key 的节点
    for (int i = skipList->level; i >= 0; --i) {
        while (current->forward[i] != NULL &&
               strcmp(current->forward[i]->key, key) < 0) {
            current = current->forward[i];
        }
        update[i] = current; // 记录每一层可能需要更新指针的位置
    }

    current = current->forward[0]; // 可能要删除的节点

    // 未找到
    if (current == NULL || strcmp(current->key, key) != 0) {
        return 1; // key 不存在
    }

    // 调整 forward 指针跳过当前节点
    for (int i = 0; i <= skipList->level; ++i) {
        if (update[i]->forward[i] != current)
            break;
        update[i]->forward[i] = current->forward[i];
    }

    // 释放当前节点资源
    kvs_free(current->key);
    kvs_free(current->value);
    kvs_free(current->forward);
    kvs_free(current);

    // 更新跳表层级（若最顶层变空）
    while (skipList->level > 0 && skipList->header->forward[skipList->level] == NULL) {
        skipList->level--;
    }

    return 0; // 删除成功
}


int kvs_skip_exist(kvs_skip_t *skipList, char *key) {
    if (!skipList || !key) return -1;

    char *value = kvs_skip_get(skipList, key);
    if (!value)
        return 1; // 不存在
    else
        return 0; // 存在
}



#if 0
int main() {
    SkipList* skipList = kvs_skip_create();
    
    insert(skipList, 3, 30);
    insert(skipList, 6, 60);
    insert(skipList, 2, 20);
    insert(skipList, 4, 40);

    display(skipList);

    search(skipList, 3);
    search(skipList, 7);

   return 0;
}
#endif




