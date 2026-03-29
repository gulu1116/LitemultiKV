#include "kvstore.h"

#if ENABLE_ARRAY
extern kvs_array_t global_array;
#endif

#if ENABLE_RBTREE
extern kvs_rbtree_t global_rbtree;
#endif

#if ENABLE_HASH
extern kvs_hash_t global_hash;
#endif

#if ENABLE_SKIP
extern kvs_skip_t global_skip;
#endif

#if ENABLE_CACHE
extern int kvs_cache_init(int max_items);
extern void kvs_cache_destroy(void);
extern void kvs_cache_record_access(const char *key);
extern void kvs_cache_touch_lru(const char *key);
extern char *kvs_cache_lru_evict_key(void);
extern int kvs_cache_lru_need_evict(void);
extern void kvs_cache_lru_add(const char *key, void *value, int value_len);
extern const char *kvs_cache_policy_name(int policy);
extern void kvs_cache_set_policy(int policy);
extern int kvs_cache_get_policy(void);
extern void kvs_cache_get_stats(char *out, int max_len);
extern void kvs_cache_ai_recommend(char *out, int max_len);
extern void kvs_cache_record_hit(void);
extern void kvs_cache_record_miss(void);
#endif

void *kvs_malloc(size_t size)
{
	return malloc(size);
}

void *kvs_calloc(size_t nmemb, size_t size)
{
	return calloc(nmemb, size);
}

void kvs_free(void *ptr)
{
	return free(ptr);
}

char *kvs_strdup(const char *s)
{
	if (!s)
		return NULL;
	size_t len = strlen(s) + 1;
	char *dup = (char *)kvs_malloc(len);
	if (dup)
		memcpy(dup, s, len);
	return dup;
}

const char *command[] = {

	"SET",
	"GET",
	"DEL",
	"MOD",
	"EXIST",
	"RSET",
	"RGET",
	"RDEL",
	"RMOD",
	"REXIST",
	"HSET",
	"HGET",
	"HDEL",
	"HMOD",
	"HEXIST",
	"SSET",
	"SGET",
	"SDEL",
	"SMOD",
	"SEXIST",
	"POLICY",
	"STATS",
	"RECOMMEND",
};

enum
{
	KVS_CMD_START = 0,
	// array
	KVS_CMD_SET = KVS_CMD_START,
	KVS_CMD_GET,
	KVS_CMD_DEL,
	KVS_CMD_MOD,
	KVS_CMD_EXIST,
	// rbtree
	KVS_CMD_RSET,
	KVS_CMD_RGET,
	KVS_CMD_RDEL,
	KVS_CMD_RMOD,
	KVS_CMD_REXIST,
	// hash
	KVS_CMD_HSET,
	KVS_CMD_HGET,
	KVS_CMD_HDEL,
	KVS_CMD_HMOD,
	KVS_CMD_HEXIST,
	// skiptable
	KVS_CMD_SSET,
	KVS_CMD_SGET,
	KVS_CMD_SDEL,
	KVS_CMD_SMOD,
	KVS_CMD_SEXIST,
	// cache/policy
	KVS_CMD_POLICY,
	KVS_CMD_STATS,
	KVS_CMD_RECOMMEND,

	KVS_CMD_COUNT,
};

const char *response[] = {

};

int kvs_split_token(char *msg, char *tokens[])
{

	if (msg == NULL || tokens == NULL)
		return -1;

	int idx = 0;
	char *token = strtok(msg, " "); // 空格分割字符串

	while (token != NULL)
	{
		// printf("idx: %d, token: %s\n", idx, token);

		tokens[idx++] = token;
		token = strtok(NULL, " ");
	}

	return idx;
}

// SET Key Value
// tokens[0] : SET
// tokens[1] : Key
// tokens[2] : Value

int kvs_filter_protocol(char **tokens, int count, char *response)
{

	if (tokens[0] == NULL || count == 0 || response == NULL)
		return -1;

	int cmd = KVS_CMD_START;
	for (cmd = KVS_CMD_START; cmd < KVS_CMD_COUNT; cmd++)
	{
		if (strcmp(tokens[0], command[cmd]) == 0)
		{
			break;
		}
	}

	int length = 0;
	int ret = 0;
	char *key = tokens[1];
	char *value = tokens[2];

	switch (cmd)
	{
#if ENABLE_ARRAY
	case KVS_CMD_SET:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_array_set(&global_array, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "EXIST\r\n");
		}
		break;

	case KVS_CMD_GET:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		char *result = NULL;

#if ENABLE_CACHE
		/* 先查缓存 */
		int cache_len = 0;
		void *cached = kvs_cache_get(actual_key, &cache_len);
		if (cached)
		{
			/* 缓存命中 */
			length = sprintf(response, "%s\r\n", (char *)cached);
			break;
		}
#endif

		/* 缓存未命中，查存储引擎 */
		result = kvs_array_get(&global_array, actual_key);
		if (result == NULL)
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		else
		{
#if ENABLE_CACHE
			/* 加入缓存 */
			kvs_cache_lru_add(actual_key, result, strlen(result) + 1);
#endif
			length = sprintf(response, "%s\r\n", result);
		}
		break;
	}
	case KVS_CMD_DEL:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_array_del(&global_array, actual_key);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			/* 删除数据时同时删除缓存 */
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_MOD:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_array_mod(&global_array, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			/* 修改成功后删除缓存，下次 GET 会重新缓存新值 */
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_EXIST:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_array_exist(&global_array, actual_key);
		if (ret == 0)
		{
			length = sprintf(response, "EXIST\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;
#endif

#if ENABLE_RBTREE
	case KVS_CMD_RSET:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_rbtree_set(&global_rbtree, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "EXIST\r\n");
		}

		break;
	case KVS_CMD_RGET:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		char *result = NULL;

#if ENABLE_CACHE
		/* 先查缓存 */
		int cache_len = 0;
		void *cached = kvs_cache_get(actual_key, &cache_len);
		if (cached)
		{
			length = sprintf(response, "%s\r\n", (char *)cached);
			break;
		}
#endif

		result = kvs_rbtree_get(&global_rbtree, actual_key);
		if (result == NULL)
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		else
		{
#if ENABLE_CACHE
			kvs_cache_lru_add(actual_key, result, strlen(result) + 1);
#endif
			length = sprintf(response, "%s\r\n", result);
		}
		break;
	}
	case KVS_CMD_RDEL:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_rbtree_del(&global_rbtree, actual_key);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_RMOD:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_rbtree_mod(&global_rbtree, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_REXIST:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_rbtree_exist(&global_rbtree, actual_key);
		if (ret == 0)
		{
			length = sprintf(response, "EXIST\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;
#endif

#if ENABLE_HASH
	case KVS_CMD_HSET:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_hash_set(&global_hash, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "EXIST\r\n");
		}
		break;

	case KVS_CMD_HGET:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		char *result = NULL;

#if ENABLE_CACHE
		/* 先查缓存 */
		int cache_len = 0;
		void *cached = kvs_cache_get(actual_key, &cache_len);
		if (cached)
		{
			length = sprintf(response, "%s\r\n", (char *)cached);
			break;
		}
#endif

		result = kvs_hash_get(&global_hash, actual_key);
		if (result == NULL)
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		else
		{
#if ENABLE_CACHE
			kvs_cache_lru_add(actual_key, result, strlen(result) + 1);
#endif
			length = sprintf(response, "%s\r\n", result);
		}
		break;
	}
	case KVS_CMD_HDEL:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_hash_del(&global_hash, actual_key);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_HMOD:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_hash_mod(&global_hash, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_HEXIST:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_hash_exist(&global_hash, actual_key);
		if (ret == 0)
		{
			length = sprintf(response, "EXIST\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;
#endif

#if ENABLE_SKIP
	case KVS_CMD_SSET:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_skip_set(&global_skip, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "EXIST\r\n");
		}
		break;

	case KVS_CMD_SGET:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		char *result = NULL;

#if ENABLE_CACHE
		/* 先查缓存 */
		int cache_len = 0;
		void *cached = kvs_cache_get(actual_key, &cache_len);
		if (cached)
		{
			length = sprintf(response, "%s\r\n", (char *)cached);
			break;
		}
#endif

		result = kvs_skip_get(&global_skip, actual_key);
		if (result == NULL)
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		else
		{
#if ENABLE_CACHE
			kvs_cache_lru_add(actual_key, result, strlen(result) + 1);
#endif
			length = sprintf(response, "%s\r\n", result);
		}
		break;
	}

	case KVS_CMD_SDEL:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_skip_del(&global_skip, actual_key);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_SMOD:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_skip_mod(&global_skip, actual_key, value);
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else if (ret == 0)
		{
#if ENABLE_CACHE
			kvs_cache_del(actual_key);
#endif
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;

	case KVS_CMD_SEXIST:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_skip_exist(&global_skip, actual_key);
		if (ret == 0)
		{
			length = sprintf(response, "EXIST\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;
#endif

#if ENABLE_CACHE
	case KVS_CMD_POLICY:
	{
		/* POLICY SET <lru|lfu|arc> / POLICY GET */
		if (count >= 3 && strcmp(tokens[1], "SET") == 0)
		{
			if (strcmp(tokens[2], "lru") == 0)
			{
				kvs_cache_set_policy(EVICT_LRU);
				length = sprintf(response, "Policy set to LRU\r\n");
			}
			else if (strcmp(tokens[2], "lfu") == 0)
			{
				kvs_cache_set_policy(EVICT_LFU);
				length = sprintf(response, "Policy set to LFU\r\n");
			}
			else if (strcmp(tokens[2], "arc") == 0)
			{
				kvs_cache_set_policy(EVICT_ARC);
				length = sprintf(response, "Policy set to ARC\r\n");
			}
			else
			{
				length = sprintf(response, "Unknown policy: %s (use lru|lfu|arc)\r\n", tokens[2]);
			}
		}
		else
		{
			const char *name = kvs_cache_policy_name(kvs_cache_get_policy());
			length = sprintf(response, "Current policy: %s\r\n", name);
		}
		break;
	}
	case KVS_CMD_STATS:
	{
		char buf[1024];
		kvs_cache_get_stats(buf, sizeof(buf));
		length = sprintf(response, "%s", buf);
		break;
	}
	case KVS_CMD_RECOMMEND:
	{
		char buf[1024];
		kvs_cache_ai_recommend(buf, sizeof(buf));
		length = sprintf(response, "%s", buf);
		break;
	}
#endif

	default:
		length = sprintf(response, "Unknown command\r\n");
		break;
	}

	return length;
}

// 协议解析 入口函数
/*
 * 1. msg: request message
 * 2. length: length of request message
 * 3. out: response message need to send
 * @return: length of response message
 */

int kvs_protocol(char *msg, int length, char *response)
{

	// SET Key Value
	// GET Key
	// DEL Key
	if (msg == NULL || length <= 0 || response == NULL)
		return -1;

	// printf("recv %d : %s\n", length, msg);

	char *tokens[KVS_MAX_TOKENS] = {0};

	int count = kvs_split_token(msg, tokens);
	if (count == -1)
		return -1;

	// memcpy(response, msg, length);

	return kvs_filter_protocol(tokens, count, response); // 解析协议
}

int init_kvengine(void)
{

#if ENABLE_ARRAY
	memset(&global_array, 0, sizeof(kvs_array_t));
	kvs_array_create(&global_array);
#endif

#if ENABLE_RBTREE
	memset(&global_rbtree, 0, sizeof(kvs_rbtree_t));
	kvs_rbtree_create(&global_rbtree);
#endif

#if ENABLE_HASH
	memset(&global_hash, 0, sizeof(kvs_hash_t));
	kvs_hash_create(&global_hash);
#endif

#if ENABLE_SKIP
	memset(&global_skip, 0, sizeof(kvs_skip_t));
	kvs_skip_create(&global_skip);
#endif

#if ENABLE_CACHE
	kvs_cache_init(1000);
#endif

	// rdb_load_default();

	return 0;
}

void dest_kvengine(void)
{

#if ENABLE_ARRAY
	kvs_array_destroy(&global_array);
#endif

#if ENABLE_RBTREE
	kvs_rbtree_destroy(&global_rbtree);
#endif

#if ENABLE_HASH
	kvs_hash_destroy(&global_hash);
#endif

#if ENABLE_SKIP
	kvs_skip_destroy(&global_skip);
#endif

#if ENABLE_CACHE
	kvs_cache_destroy();
#endif
}

// port, kvs_protocol
int main(int argc, char *argv[])
{

	if (argc != 2)
		return -1;

	int port = atoi(argv[1]);

	init_kvengine();

#if (NETWORK_SELECT == NETWORK_REACTOR)

	reactor_start(port, kvs_protocol);

#elif (NETWORK_SELECT == NETWORK_NTYCO)

	ntyco_start(port, kvs_protocol);

#elif (NETWORK_SELECT == NETWORK_PROACTOR)

	proactor_start(port, kvs_protocol);

#endif

	dest_kvengine();

	return 0;
}