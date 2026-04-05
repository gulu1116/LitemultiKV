# LiteMultiKV 性能基准测试

标准化的核心性能数据采集工具，覆盖引擎对比和网络框架对比。

## 采集指标

| 指标 | 说明 |
|------|------|
| **QPS** | 每秒操作数（总 QPS + 每操作 QPS） |
| **平均延迟** | 单次操作平均耗时 (μs) |
| **P50 / P99 延迟** | 延迟百分位数 (μs) |
| **CPU 占用率** | 服务端进程 CPU% (基于 /proc/[pid]/stat) |
| **内存 RSS** | 服务端进程驻留内存 (MB) |

## 测试维度

| 维度 | 覆盖内容 |
|------|----------|
| **引擎对比** | Array vs RBTree vs Hash vs SkipList |
| **操作对比** | SET / GET / MOD / EXIST / DEL（每引擎独立测试） |
| **网络框架对比** | Reactor (epoll) vs Proactor (io_uring) |

---

## 目录结构

```
perf/
├── benchmark.py       # 核心基准测试脚本
├── run_benchmark.sh   # 一键运行封装
├── README.md
└── output/            # 测试报告（自动生成）
    └── benchmark_YYYYMMDD_HHMMSS.json
```

---

## 快速开始

```bash
cd perf/

# 默认: reactor 模式，全部引擎，10000 次/操作
./run_benchmark.sh

# reactor + proactor 网络框架对比
./run_benchmark.sh --network all

# 单引擎测试
./run_benchmark.sh --engine hash

# 大规模测试
./run_benchmark.sh --count 50000
```

## 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--port PORT` | 18888 | 服务端口 |
| `--count N` | 10000 | 每引擎每操作执行次数 |
| `--engine ENGINE` | all | `array` / `rbtree` / `hash` / `skiplist` / `all` |
| `--network MODEL` | reactor | `reactor` / `proactor` / `all` |

## 输出示例

### 引擎性能对比

```
  引擎         总QPS    SET QPS    GET QPS   CPU%  RSS(MB)
  ──────────────────────────────────────────────────────
  array       27,044     22,170     26,444  80.9%     4.1
  rbtree      77,881     89,933     66,763  57.2%     4.7
  hash        65,129     65,111     52,293  59.6%     4.7
  skiplist    71,705     86,496     57,213  61.3%     4.7
```

### 操作延迟明细

```
  操作     QPS       平均延迟    P99延迟    耗时(ms)
  ──────────────────────────────────────────────────
  SET    89,933     10.6us     22.7us      111.2
  GET    66,763     14.5us     33.6us      149.8
  MOD    63,731     15.1us     37.2us      156.9
  EXIST 107,309      9.0us     11.6us       93.2
  DEL    76,394     12.5us     33.2us      130.9
```

### 网络框架对比

```
  引擎        REACTOR QPS  REACTOR CPU%   PROACTOR QPS  PROACTOR CPU%
  ──────────────────────────────────────────────────────────────────
  array          26,556       81.1%          32,258       81.7%
  rbtree         76,324       59.1%          82,074       57.0%
  hash           67,980       62.2%          69,686       63.7%
  skiplist       69,003       60.4%          73,046       58.0%
```

## JSON 报告

每次运行自动保存至 `output/benchmark_YYYYMMDD_HHMMSS.json`，结构：

```json
{
  "timestamp": "20260405_162513",
  "results": {
    "reactor": {
      "array": {
        "engine": "array",
        "network": "reactor",
        "total_ops": 50000,
        "overall_qps": 27044,
        "cpu_pct": 80.9,
        "mem_rss_mb": 4.1,
        "operations": {
          "SET": {"qps": 22170, "avg_latency_us": 44.3, "p99_latency_us": 137.0},
          "GET": {"qps": 26444, "avg_latency_us": 37.3, "p99_latency_us": 84.3}
        }
      }
    }
  }
}
```

## 测试方法说明

- 每引擎使用 **独立 TCP 连接**，避免跨引擎干扰
- 每操作使用 **不同 key**（`key0` ~ `key{N-1}`），测试真实数据规模下的性能
- 测试顺序: SET → GET → MOD → EXIST → DEL（保证数据依赖正确）
- 预热 100 次操作后再计时
- CPU% 通过 `/proc/[pid]/stat` 的 utime+stime 计算
- 内存通过 `/proc/[pid]/status` 的 VmRSS 读取
- 自动编译（`make clean` + `make <target>`）确保二进制与网络模型一致
