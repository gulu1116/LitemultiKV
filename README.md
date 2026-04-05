# LiteMultiKV

Lightweight multi-engine KV store | Multi-engine · Vector Search · Smart Cache · Multi-tenant

---

## 目录

- [一、核心特性](#一、核心特性)
- [二、快速开始](#二、快速开始)
- [三、命令详解](#三、命令详解)
- [四、缓存系统](#四、缓存系统)
- [五、向量检索](#五、向量检索)
- [六、命名空间](#六、命名空间)
- [七、持久化](#七、持久化)
- [八、性能测试](#八、性能测试)
- [九、客户端开发](#九、客户端开发)

---

## 一、核心特性

| 特性 | 说明 |
|------|------|
| **多存储引擎** | Array、Hash、RBTree、SkipList 四种引擎，适配不同场景 |
| **向量检索** | 基于 HNSW 算法的高维向量近似最近邻搜索 |
| **智能缓存** | LRU/LFU/ARC 三种策略 + AI 自动调优建议 |
| **命名空间隔离** | 多租户数据隔离，支持独立命名空间 |
| **数据持久化** | RDB 格式持久化，支持同步/异步保存 |
| **高性能** | QPS 可达 50,000+，低延迟响应 |

---

## 二、快速开始

### 环境要求

- Linux 操作系统（推荐 Ubuntu 20.04+）
- GCC 9.4.0+
- Python 3.x（用于测试脚本）

### 安装步骤

```bash
# 1. 安装依赖
sudo apt update
sudo apt install gcc make liburing-dev python3

# 2. 克隆项目
cd /path/to/LitemultiKV-main

# 3. 编译
make

# 4. 启动服务器
./kvstore 19999
```

### 验证安装

```bash
# 新开终端，运行测试
python3 tests/test_comprehensive.py
```

预期输出：
```
Passed: 75, Failed: 0, Success Rate: 100.0%
```

---

## 三、命令详解

### 基础 KV 操作

LiteMultiKV 提供四种存储引擎，每种引擎有独立的命令前缀：

| 引擎 | 命令前缀 | 特点 | 适用场景 |
|------|----------|------|----------|
| **Array** | 无前缀 | 简单线性结构 | 小规模数据 |
| **Hash** | H | O(1) 访问 | 高频读写 |
| **RBTree** | R | 有序存储 | 范围查询 |
| **SkipList** | S | 有序 + 高效插入 | 大规模有序数据 |

#### 通用操作格式

```
# Array 引擎（默认）
SET <key> <value>    # 设置键值
GET <key>            # 获取值
DEL <key>            # 删除键
MOD <key> <value>    # 修改值
EXIST <key>          # 检查是否存在

# Hash 引擎（加 H 前缀）
HSET <key> <value>
HGET <key>
HDEL <key>
HMOD <key> <value>
HEXIST <key>

# RBTree 引擎（加 R 前缀）
RSET <key> <value>
RGET <key>
RDEL <key>
RMOD <key> <value>
REXIST <key>

# SkipList 引擎（加 S 前缀）
SSET <key> <value>
SGET <key>
SDEL <key>
SMOD <key> <value>
SEXIST <key>
```

#### 使用示例

```bash
# 连接服务器（使用 nc 或 telnet）
nc 127.0.0.1 19999

# Array 引擎操作
SET user:1 "Alice"
GET user:1          # 返回: "Alice"
MOD user:1 "Bob"
GET user:1          # 返回: "Bob"
DEL user:1
GET user:1          # 返回: "NO EXIST"

# Hash 引擎操作
HSET session:abc "token123"
HGET session:abc    # 返回: "token123"

# RBTree 引擎操作（支持有序遍历）
RSET score:alice 95
RSET score:bob 87
RGET score:alice    # 返回: 95

# SkipList 引擎操作
SSET log:1 "error message"
SGET log:1
```

---

### 向量操作

基于 HNSW（Hierarchical Navigable Small World）算法的向量检索引擎。

| 命令 | 格式 | 说明 |
|------|------|------|
| VSET | `VSET <key> <dim> <v1> <v2> ...` | 插入向量 |
| VGET | `VGET <key>` | 获取向量信息 |
| VSEARCH | `VSEARCH <key> <k> <dim> <v1> ...` | 搜索相似向量 |
| VDEL | `VDEL <key>` | 删除向量 |
| VCOUNT | `VCOUNT` | 统计向量数量 |

#### 使用示例

```bash
# 插入 3 维向量
VSET vec:1 3 1.0 2.0 3.0
VSET vec:2 3 1.1 2.1 3.1
VSET vec:3 3 5.0 5.0 5.0

# 获取向量信息
VGET vec:1
# 返回: key=vec:1, dim=3, vector=[1.00, 2.00, 3.00]

# 搜索最相似的 2 个向量
VSEARCH vec:1 2 3 1.0 2.0 3.0
# 返回: RESULT
#       0 0.0000
#       1 0.1732

# 统计向量数量
VCOUNT
# 返回: 3

# 删除向量
VDEL vec:1
```

---

## 四、缓存系统

LiteMultiKV 内置智能缓存系统，支持三种淘汰策略和 AI 自动调优。

### 缓存策略

| 策略 | 算法 | 适用场景 |
|------|------|----------|
| **LRU** | Least Recently Used | 时间局部性强的访问模式 |
| **LFU** | Least Frequently Used | 热点数据稳定的场景 |
| **ARC** | Adaptive Replacement Cache | 访问模式动态变化的场景 |

### 缓存命令

```bash
# 查看当前策略
POLICY
# 返回: Current policy: LRU

# 设置策略
POLICY SET lru
POLICY SET lfu
POLICY SET arc

# 查看缓存统计
STATS

# 获取 AI 调优建议
RECOMMEND
```

### AI 调优功能

系统会自动分析访问模式，提供智能调优建议：

```
=== AI Cache Recommendation (Enhanced) ===

[Current Status]
  Policy: LRU
  Hit Rate: 45.2% (Predicted: 48.5%)
  Peak QPS: 1200, Avg QPS: 350
  Peak/Avg Ratio: 3.43x

[Pattern Analysis]
  Detected Pattern: Bursty
  Dynamic Hit Rate Threshold: 50.0%
  Dynamic Burst Threshold: 5.0x
  History Samples: 15

[Recommendation]
  Recommended Policy: ARC
  Recommended Max Items: 2000
  Confidence: High
  Reason: Bursty access pattern detected
  Detail: ARC dual-buffer adapts well to sudden traffic spikes

[Actions]
  To apply: POLICY SET ARC
  To view stats: STATS
```

### AI 调优特性

| 特性 | 说明 |
|------|------|
| **动态阈值调整** | 根据历史数据自动调整决策阈值 |
| **访问模式识别** | 识别 Stable/Bursty/Periodic/Random 四种模式 |
| **趋势预测** | 基于指数平滑预测未来命中率 |
| **置信度评估** | High/Medium/Low 三级置信度 |

---

## 五、命名空间

支持多租户数据隔离，不同命名空间的数据完全独立。

### 命名空间命令

```bash
# 查看当前命名空间
NAMESPACE
# 返回: Current namespace: default

# 切换命名空间
NAMESPACE SET tenant_A
# 返回: Namespace set to: tenant_A

# 重置到默认命名空间
NAMESPACE RESET
# 返回: Namespace reset to: default

# 简写形式
NS SET tenant_B
NS RESET
```

### 使用示例

```bash
# 在 default 命名空间设置数据
SET config:db "mysql"

# 切换到 tenant_A 命名空间
NAMESPACE SET tenant_A

# 在 tenant_A 中设置同名键
SET config:db "postgresql"

# 验证隔离
GET config:db
# 返回: "postgresql"（tenant_A 的数据）

# 切回 default
NAMESPACE RESET
GET config:db
# 返回: "mysql"（default 的数据）
```

---

## 六、持久化

支持 RDB 格式的数据持久化，服务重启后自动恢复。

### 持久化命令

```bash
# 同步保存（阻塞）
SAVE
# 返回: OK

# 后台异步保存
BGSAVE
# 返回: Background saving started

# 查看上次保存时间
LASTSAVE
# 返回: Last save: 1774703572 (timestamp)
```

### 自动加载

服务器启动时会自动检测并加载 `dump.rdb` 文件：

```bash
./kvstore 19999
# 输出: RDB: Loading from dump.rdb...
# 输出: RDB: Loaded 6 keys from dump.rdb
```

---

## 七、性能测试

### 功能测试

```bash
# 启动服务器
./kvstore 19999

# 运行综合测试（另一个终端）
python3 tests/test_comprehensive.py
```

### 功能测试覆盖

| 测试类型 | 测试项 |
|----------|--------|
| 基础操作 | 四种引擎的 SET/GET/DEL/MOD/EXIST |
| 向量操作 | VSET/VGET/VSEARCH/VDEL/VCOUNT |
| 缓存策略 | LRU/LFU/ARC 切换与统计 |
| 命名空间 | 隔离验证、多引擎支持 |
| 持久化 | SAVE/BGSAVE/dump.rdb |
| 边界条件 | 空命令、不存在的键、长键值 |
| 并发测试 | 多线程并发操作 |

### 性能基准测试

项目提供标准化的性能基准测试工具（`perf/`），采集 QPS、延迟、CPU 占用、内存等核心指标。

```bash
# 一键运行全引擎基准测试（自动编译 + 启停服务）
cd perf/
./run_benchmark.sh

# Reactor vs Proactor 网络框架对比
./run_benchmark.sh --network all

# 自定义参数
./run_benchmark.sh --engine hash --count 50000
```

#### 引擎性能对比 (Reactor, 10K ops/操作)

| 引擎 | 总 QPS | SET QPS | GET QPS | CPU% | RSS |
|------|--------|---------|---------|------|-----|
| Array | 27,044 | 22,170 | 26,444 | 80.9% | 4.1 MB |
| RBTree | 77,881 | 89,933 | 66,763 | 57.2% | 4.7 MB |
| Hash | 65,129 | 65,111 | 52,293 | 59.6% | 4.7 MB |
| SkipList | 71,705 | 86,496 | 57,213 | 61.3% | 4.7 MB |

#### 网络框架对比

| 引擎 | Reactor QPS | Proactor QPS |
|------|-------------|--------------|
| Array | 26,556 | 32,258 |
| RBTree | 76,324 | 82,074 |
| Hash | 67,980 | 69,686 |
| SkipList | 69,003 | 73,046 |

详见 [`perf/README.md`](perf/README.md)。

### C 语言性能测试工具

```bash
./testcase 127.0.0.1 19999
```

---

## 八、客户端开发

### Python 客户端示例

```python
import socket

class KVStoreClient:
    def __init__(self, host='127.0.0.1', port=19999):
        self.host = host
        self.port = port
        self.sock = None
    
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
    
    def close(self):
        if self.sock:
            self.sock.close()
    
    def send(self, cmd):
        self.sock.sendall((cmd + "\r\n").encode())
        return self.sock.recv(8192).decode().strip()

# 使用示例
client = KVStoreClient()
client.connect()

print(client.send("SET key1 value1"))  # OK
print(client.send("GET key1"))          # value1
print(client.send("DEL key1"))          # OK

client.close()
```

### Go 客户端示例

```go
package main

import (
    "bufio"
    "fmt"
    "net"
)

func main() {
    conn, _ := net.Dial("tcp", "127.0.0.1:19999")
    defer conn.Close()
    
    // 发送命令
    fmt.Fprintf(conn, "SET key1 value1\r\n")
    
    // 读取响应
    reader := bufio.NewReader(conn)
    response, _ := reader.ReadString('\n')
    fmt.Println(response)
}
```

---

## 九、项目结构

```
LitemultiKV-main/
├── include/                 # 公共头文件
│   ├── kvstore.h            # 核心数据结构与 API 声明
│   └── server.h             # 网络连接结构定义
├── src/
│   ├── common/
│   │   └── kvstore.c        # 主服务程序（命令解析、引擎初始化）
│   ├── engine/
│   │   ├── kvs_array.c      # 数组存储引擎
│   │   ├── kvs_hash.c       # 哈希存储引擎
│   │   ├── kvs_rbtree.c     # 红黑树存储引擎
│   │   ├── kvs_skiptable.c  # 跳表存储引擎
│   │   └── kvs_vector.c     # 向量检索引擎 (HNSW)
│   ├── cache/
│   │   └── kvs_cache.c      # 缓存系统（LRU/LFU/ARC + AI 调优）
│   ├── network/
│   │   ├── reactor.c        # Reactor 网络模型
│   │   ├── proactor.c       # Proactor 网络模型 (io_uring)
│   │   └── ntyco.c          # 协程网络模型
│   └── persist/
│       └── kvs_rdb.c        # RDB 持久化
├── tests/
│   ├── testcase.c           # C 性能测试工具
│   ├── test_comprehensive.py # Python 综合测试
│   └── test_kvstore.py      # Python 功能测试
├── NtyCo/                   # 协程库
├── kvs-client/              # 多语言客户端示例
├── docs/                    # 设计文档
├── perf/                    # 性能采集，详见 [perf/README.md]
└── Makefile
```

---

## 十、常见问题

### 编译错误

**问题**：缺少 liburing 库
```bash
# 解决方案
sudo apt install liburing-dev
```

**问题**：链接错误
```bash
# 解决方案
make clean && make
```

### 运行问题

**问题**：端口被占用
```bash
# 查找占用进程
netstat -tulnp | grep 19999
# 终止进程
kill <pid>
```

**问题**：性能下降
```bash
# 检查系统负载
top -p $(pgrep kvstore)

# 增加文件描述符限制
ulimit -n 100000
```

---

## 十一、开发调试

```bash
# 调试模式编译
make CFLAGS="-g -O0"

# 使用 GDB 调试
gdb --args ./kvstore 19999

# 查看内存泄漏
valgrind --leak-check=full ./kvstore 19999
```

---

## 十二、许可证

本项目仅供学习和研究使用。
