#include "kvstore.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

#if ENABLE_VECTOR
extern kvs_vector_t global_vector;
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
	"VSET",
	"VGET",
	"VSEARCH",
	"VDEL",
	"VCOUNT",
	"POLICY",
	"STATS",
	"RECOMMEND",
	"NAMESPACE",
	"NS",
	"SAVE",
	"BGSAVE",
	"LASTSAVE",
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
	// vector
	KVS_CMD_VSET,
	KVS_CMD_VGET,
	KVS_CMD_VSEARCH,
	KVS_CMD_VDEL,
	KVS_CMD_VCOUNT,
	// cache/policy
	KVS_CMD_POLICY,
	KVS_CMD_STATS,
	KVS_CMD_RECOMMEND,
	// namespace
	KVS_CMD_NAMESPACE,
	KVS_CMD_NS,
	// persistence
	KVS_CMD_SAVE,
	KVS_CMD_BGSAVE,
	KVS_CMD_LASTSAVE,

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

static char *make_namespaced_key(const char *namespace, const char *key, char *buf, int buf_size)
{
	if (namespace == NULL || strlen(namespace) == 0 || strcmp(namespace, "default") == 0)
	{
		return (char *)key;
	}
	snprintf(buf, buf_size, "%s:%s", namespace, key);
	return buf;
}

int kvs_filter_protocol(char **tokens, int count, char *response, char *namespace)
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
	char namespaced_key[256] = {0};
	char *actual_key = key;

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

#if ENABLE_VECTOR
	case KVS_CMD_VSET:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		/* VSET key dim val1 val2 ... - 需要合并 value 部分 */
		/* tokens[0]=VSET, tokens[1]=key, tokens[2]=dim, tokens[3+]=向量数据 */
		if (count >= 4)
		{
			/* 合并从 tokens[2] 开始的所有内容为 value 字符串 */
			static char vset_value[8192];
			vset_value[0] = '\0';
			for (int i = 2; i < count; i++)
			{
				if (i > 2)
					strcat(vset_value, " ");
				strcat(vset_value, tokens[i]);
			}
			ret = kvs_vector_set(&global_vector, actual_key, vset_value);
		}
		else
		{
			ret = -1;
		}
		if (ret < 0)
		{
			length = sprintf(response, "ERROR\r\n");
		}
		else
		{
			length = sprintf(response, "OK\r\n");
		}
		break;
	}
	case KVS_CMD_VGET:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		char *result = kvs_vector_get(&global_vector, actual_key);
		if (result == NULL)
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		else
		{
			length = sprintf(response, "%s\r\n", result);
		}
		break;
	}
	case KVS_CMD_VSEARCH:
	{
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		/* VSEARCH key k query_dim val1 val2 ... */
		/* tokens[0]=VSEARCH, tokens[1]=key, tokens[2]=k, tokens[3]=dim, tokens[4+]=查询向量 */
		if (count >= 5)
		{
			int k = atoi(tokens[2]);
			if (k <= 0 || k > 100)
				k = 10;
			int result_ids[100];
			float result_dists[100];
			/* 合并查询向量部分 (tokens[4] onwards) */
			static char query_str[8192];
			query_str[0] = '\0';
			for (int i = 4; i < count; i++)
			{
				if (i > 4)
					strcat(query_str, " ");
				strcat(query_str, tokens[i]);
			}
			int n = kvs_vector_search(&global_vector, actual_key, k, query_str, result_ids, result_dists);
			if (n <= 0)
			{
				length = sprintf(response, "EMPTY\r\n");
			}
			else
			{
				length = sprintf(response, "RESULT\r\n");
				for (int i = 0; i < n; i++)
				{
					length += sprintf(response + length, "%d %.4f\r\n", result_ids[i], result_dists[i]);
				}
			}
		}
		else
		{
			length = sprintf(response, "ERROR\r\n");
		}
		break;
	}
	case KVS_CMD_VDEL:
		actual_key = make_namespaced_key(namespace, key, namespaced_key, sizeof(namespaced_key));
		ret = kvs_vector_del(&global_vector, actual_key);
		if (ret == 0)
		{
			length = sprintf(response, "OK\r\n");
		}
		else
		{
			length = sprintf(response, "NO EXIST\r\n");
		}
		break;
	case KVS_CMD_VCOUNT:
	{
		int vcount = kvs_vector_count(&global_vector);
		length = sprintf(response, "%d\r\n", vcount);
		break;
	}
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

	case KVS_CMD_NAMESPACE:
	case KVS_CMD_NS:
	{
		if (count >= 2 && strcmp(tokens[1], "SET") == 0 && count >= 3)
		{
			strncpy(namespace, tokens[2], 63);
			namespace[63] = '\0';
			length = sprintf(response, "Namespace set to: %s\r\n", namespace);
		}
		else if (count >= 2 && strcmp(tokens[1], "RESET") == 0)
		{
			strcpy(namespace, "default");
			length = sprintf(response, "Namespace reset to: default\r\n");
		}
		else
		{
			length = sprintf(response, "Current namespace: %s\r\n", namespace ? namespace : "default");
		}
		break;
	}

	case KVS_CMD_SAVE:
	{
		int ret = rdb_save_default();
		if (ret >= 0)
		{
			length = sprintf(response, "OK: Saved %d keys to dump.rdb\r\n", ret);
		}
		else
		{
			length = sprintf(response, "ERROR: Failed to save\r\n");
		}
		break;
	}

	case KVS_CMD_BGSAVE:
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			int ret = rdb_save_default();
			_exit(ret >= 0 ? 0 : 1);
		}
		else if (pid > 0)
		{
			length = sprintf(response, "Background saving started (pid: %d)\r\n", pid);
		}
		else
		{
			length = sprintf(response, "ERROR: Fork failed\r\n");
		}
		break;
	}

	case KVS_CMD_LASTSAVE:
	{
		struct stat st;
		if (stat("dump.rdb", &st) == 0)
		{
			length = sprintf(response, "Last save: %ld (timestamp)\r\n", (long)st.st_mtime);
		}
		else
		{
			length = sprintf(response, "No dump file found\r\n");
		}
		break;
	}

	default:
		length = sprintf(response, "Unknown command\r\n");
		break;
	}

	return length;
}

int kvs_protocol(char *msg, int length, char *response, char *namespace)
{

	// SET Key Value
	// GET Key
	// DEL Key
	if (msg == NULL || length <= 0 || response == NULL)
		return -1;

	// Fix: strip \r from \r\n so strtok sees clean spaces
	for (int i = 0; i < length; i++)
	{
		if (msg[i] == '\r')
			msg[i] = ' ';
	}

	char *tokens[KVS_MAX_TOKENS] = {0};

	int count = kvs_split_token(msg, tokens);
	if (count == -1)
		return -1;

	return kvs_filter_protocol(tokens, count, response, namespace);
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

#if ENABLE_VECTOR
	kvs_vector_create(&global_vector);
#endif

#if ENABLE_CACHE
	kvs_cache_init(1000);
#endif

	rdb_load_default();

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

#if ENABLE_VECTOR
	kvs_vector_destroy(&global_vector);
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