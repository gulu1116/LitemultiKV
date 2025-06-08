# KVStore 项目文档

## 项目概述
本项目是一个多引擎键值存储系统，支持基于数组、哈希表、红黑树和跳表四种数据结构的存储引擎实现。系统采用事件驱动模型（Reactor/Proactor模式），可在Linux环境下高效运行，并提供网络访问接口。

## 环境要求
- **操作系统**：Linux（推荐Ubuntu 20.04+或同等内核版本）
- **编译器**：gcc (GCC) 9.4.0+
- **依赖库**：
  - liburing (io_uring支持)
  - pthread
- **开发工具**：
  - VS Code（可选，用于远程开发）
  - make 构建工具

## 项目结构
```
.
├── .vscode/          # VS Code本地配置（可忽略）
├── NtyCo/            # 协程库核心实现
├── kvs-client/       # 多语言客户端示例
├── Makefile          # 构建配置文件
├── kvs_array.c       # 数组引擎实现
├── kvs_hash.c        # 哈希表引擎实现
├── kvs_rbtree.c      # 红黑树引擎实现
├── kvs_skiptable.c   # 跳表引擎实现
├── kvstore.c         # 主服务端程序
├── kvstore.h         # 公共头文件
├── ntyco.c           # 协程模块
├── proactor.c        # Proactor模式实现
├── reactor.c         # Reactor模式实现
├── server.h          # 服务端定义
└── testcase.c        # 性能测试工具
```
### 分层架构

项目采用分层设计，模块职责清晰：

|层级|描述|
|---|---|
|网络层|处理通信，支持 Reactor、Proactor 和 Ntyco 协程模型。|
|协议层|解析请求命令、格式化响应。|
|接口适配层|抽象统一接口（5 + 2 标准），屏蔽不同引擎实现差异。|
|引擎层|提供多种存储结构：数组、红黑树、哈希表。|
|核心层|管理 `kvstore` 全局实例的生命周期。|

### 模块交互流程

1. 客户端请求 →
2. 协议解析命令 →
3. 分发到指定引擎接口 →
4. 执行数据操作 →
5. 返回响应 →
6. 网络回传结果。

### 引擎层（多种数据结构支持）

- **数组（Array）**：简单线性结构，适合小规模数据。
- **红黑树（RBTree）**：平衡二叉树，支持有序操作。
- **哈希表（Hash）**：快速定位，适合高频访问。
- **跳表（SkipList）**：支持有序插入与范围查询，性能接近平衡树，结构更简单。

### 接口适配层（统一操作接口）

- 标准接口包括：`create`、`destroy`、`set`、`get`、`del`、`mod`、`exist`。
- 使用宏配置启用对应引擎，保证协议层调用无缝切换。
    

### 协议层（命令解析与响应）

- 支持基本命令格式：如 `SET key value`。
- 响应统一：如 `OK\r\n`、`EXIST\r\n`。
- 基于前缀分发：如 `HSET` 进入哈希表处理模块。
    

### 网络层（多种 I/O 模型）

- **Reactor**：事件驱动，适合高并发处理。
- **Proactor**：异步 I/O，通过回调触发。
- **Ntyco 协程**：轻量级协程，减少上下文切换。
   

## 快速开始

### 1. 安装依赖
```bash
sudo apt update
sudo apt install gcc make liburing-dev
```

### 2. 编译项目
```bash
make
```
此命令将：
1. 编译 NtyCo 协程库
2. 构建主服务程序 `kvstore`
3. 构建测试工具 `testcase`

### 3. 运行服务端
```bash
./kvstore <port>
```
- `<port>`：服务端监听端口（选择1024-65535之间的空闲端口）
- 示例：`./kvstore 8888`

### 4. 性能测试
#### 本地测试：
```bash
./testcase 127.0.0.1 <port>
```

#### 远程主机测试：
1. 复制 `testcase` 到测试主机
2. 运行测试：
```bash
./testcase <server_ip> <port>
```
- 输出结果将显示各引擎的QPS性能指标

## 客户端连接
### 多语言客户端支持
`kvs-client/` 目录包含多种语言的客户端示例：
```bash
kvs-client/
├── go-client/      # Go语言客户端
├── py-client/      # Python客户端
└── rust-client/    # Rust客户端
```

### 使用步骤
1. 进入对应语言目录
2. 修改客户端代码中的服务器地址和端口：
   ```python
   # Python示例 (kvs-client/py-client/client.py)
   SERVER_IP = '192.168.1.100'  # 修改为实际服务器IP
   PORT = 8888                  # 修改为实际端口
   ```
3. 运行客户端（需先安装对应语言环境）

## 编译系统详解
### Makefile 关键指令
| 命令                | 功能描述                     |
|---------------------|----------------------------|
| `make`             | 编译整个项目（默认目标）     |
| `make clean`       | 清理所有编译生成的文件       |
| `make testcase`    | 单独构建测试工具            |

### 编译选项
```makefile
FLAGS = -I ./NtyCo/core/ -L ./NtyCo/ -lntyco -lpthread -luring -ldl
```
- `-luring`：链接 io_uring 异步I/O库
- `-lntyco`：链接项目协程库
- `-lpthread`：支持多线程

## 开发建议
### VS Code 远程开发
1. 安装 Remote-SSH 扩展
2. 连接到 Linux 开发机
3. 打开项目目录
4. 使用集成终端执行 make 命令

### 调试技巧
```bash
# 启用调试模式编译
make CFLAGS="-g -O0"

# 使用gdb调试
gdb --args ./kvstore 8888
```

## 性能优化建议
1. 根据负载特征选择最佳引擎：
   - 小数据集：数组引擎
   - 中等规模：哈希表
   - 大规模数据：跳表或红黑树
2. 对于高并发场景：
   ```bash
   # 调整最大文件描述符数
   ulimit -n 100000
   ```
3. 使用 taskset 绑定CPU核心：
   ```bash
   taskset -c 0,1 ./kvstore 8888
   ```

## 常见问题排查
### 编译错误
1. **缺少 liburing**：
   ```bash
   sudo apt install liburing-dev
   ```
2. **链接错误**：
   ```bash
   make clean && make
   ```

### 运行时问题
1. **端口冲突**：
   ```bash
   netstat -tulnp | grep <port>
   kill <pid>
   ```
2. **性能下降**：
   - 检查系统负载：`top`/`htop`
   - 监控网络：`iftop -P`

## 清理项目
```bash
make clean
```
此命令将删除所有编译生成的文件，包括：
- 所有 `.o` 目标文件
- `kvstore` 可执行文件
- `testcase` 测试工具
