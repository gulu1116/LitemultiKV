#!/usr/bin/env python3
"""LiteMultiKV 核心性能基准测试

采集指标:
  - QPS (每秒操作数)
  - 平均延迟 / P99 延迟 (ms)
  - CPU 占用率 (%)
  - 内存占用 RSS (MB)

测试维度:
  1. 引擎对比: Array vs RBTree vs Hash vs SkipList
  2. 操作对比: SET / GET / MOD / EXIST / DEL (每引擎)
  3. 网络框架对比: Reactor (epoll) vs Proactor (io_uring)

用法:
  python3 benchmark.py                        # 默认: reactor, 全部引擎
  python3 benchmark.py --engine hash          # 单引擎测试
  python3 benchmark.py --network all          # reactor + proactor 对比
  python3 benchmark.py --count 50000          # 自定义操作数
"""
import socket
import time
import sys
import os
import json
import signal
import argparse
import subprocess
from collections import defaultdict

# ── 引擎配置 ──────────────────────────────────────────────
ENGINES = {
    "array":    {"SET": "SET",    "GET": "GET",    "MOD": "MOD",    "EXIST": "EXIST",    "DEL": "DEL"},
    "rbtree":   {"SET": "RSET",   "GET": "RGET",   "MOD": "RMOD",   "EXIST": "REXIST",   "DEL": "RDEL"},
    "hash":     {"SET": "HSET",   "GET": "HGET",   "MOD": "HMOD",   "EXIST": "HEXIST",   "DEL": "HDEL"},
    "skiplist":  {"SET": "SSET",   "GET": "SGET",   "MOD": "SMOD",   "EXIST": "SEXIST",   "DEL": "SDEL"},
}

NETWORK_MODELS = {
    "reactor":  {"binary": "kvstore",          "env": {},                                    "make_target": "reactor"},
    "proactor": {"binary": "kvstore_proactor",  "env": {"LD_LIBRARY_PATH": "../local/lib"},  "make_target": "proactor"},
}

# ── 工具函数 ──────────────────────────────────────────────
def get_project_dir():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def get_output_dir():
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "output")
    os.makedirs(d, exist_ok=True)
    return d

def read_proc_stat(pid):
    """读取 /proc/[pid]/stat 获取 CPU 时间 (utime+stime, clock ticks)"""
    try:
        with open(f"/proc/{pid}/stat") as f:
            parts = f.read().split()
            utime = int(parts[13])
            stime = int(parts[14])
            return utime + stime
    except Exception:
        return 0

def read_proc_memory(pid):
    """读取 /proc/[pid]/status 获取 VmRSS (KB)"""
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except Exception:
        pass
    return 0

def get_clock_ticks():
    try:
        return os.sysconf("SC_CLK_TCK")
    except Exception:
        return 100

# ── 网络客户端 ────────────────────────────────────────────
class KVClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect((self.host, self.port))

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def cmd(self, command):
        self.sock.sendall((command + "\r\n").encode())
        return self.sock.recv(4096).decode(errors="ignore")

    def cmd_timed(self, command):
        """发送命令并返回 (响应, 延迟us)"""
        t0 = time.perf_counter()
        self.sock.sendall((command + "\r\n").encode())
        resp = self.sock.recv(4096).decode(errors="ignore")
        dt = (time.perf_counter() - t0) * 1e6  # us
        return resp, dt


# ── 单引擎基准测试 ────────────────────────────────────────
def bench_engine(client, engine_name, ops, count):
    """对单个引擎执行全操作基准测试，返回每个操作的指标"""
    results = {}

    # SET: 插入 count 个不同 key
    latencies = []
    t0 = time.perf_counter()
    for i in range(count):
        _, dt = client.cmd_timed(f"{ops['SET']} key{i} val{i}")
        latencies.append(dt)
    elapsed = time.perf_counter() - t0
    results["SET"] = _calc_metrics("SET", count, elapsed, latencies)

    # GET: 查询刚插入的 key
    latencies = []
    t0 = time.perf_counter()
    for i in range(count):
        _, dt = client.cmd_timed(f"{ops['GET']} key{i}")
        latencies.append(dt)
    elapsed = time.perf_counter() - t0
    results["GET"] = _calc_metrics("GET", count, elapsed, latencies)

    # MOD: 修改
    latencies = []
    t0 = time.perf_counter()
    for i in range(count):
        _, dt = client.cmd_timed(f"{ops['MOD']} key{i} newval{i}")
        latencies.append(dt)
    elapsed = time.perf_counter() - t0
    results["MOD"] = _calc_metrics("MOD", count, elapsed, latencies)

    # EXIST: 检查存在性
    latencies = []
    t0 = time.perf_counter()
    for i in range(count):
        _, dt = client.cmd_timed(f"{ops['EXIST']} key{i}")
        latencies.append(dt)
    elapsed = time.perf_counter() - t0
    results["EXIST"] = _calc_metrics("EXIST", count, elapsed, latencies)

    # DEL: 删除
    latencies = []
    t0 = time.perf_counter()
    for i in range(count):
        _, dt = client.cmd_timed(f"{ops['DEL']} key{i}")
        latencies.append(dt)
    elapsed = time.perf_counter() - t0
    results["DEL"] = _calc_metrics("DEL", count, elapsed, latencies)

    return results


def _calc_metrics(op_name, count, elapsed, latencies):
    latencies.sort()
    return {
        "op": op_name,
        "count": count,
        "elapsed_ms": round(elapsed * 1000, 1),
        "qps": int(count / elapsed) if elapsed > 0 else 0,
        "avg_latency_us": round(sum(latencies) / len(latencies), 1) if latencies else 0,
        "p50_latency_us": round(latencies[len(latencies) // 2], 1) if latencies else 0,
        "p99_latency_us": round(latencies[int(len(latencies) * 0.99)], 1) if latencies else 0,
    }


# ── 服务器管理 ────────────────────────────────────────────
class ServerManager:
    def __init__(self, project_dir, model, port):
        self.project_dir = project_dir
        self.model = model
        self.port = port
        self.proc = None
        self.cfg = NETWORK_MODELS[model]

    def build(self):
        """编译对应的网络模型二进制（自动 clean 避免 .o 冲突）"""
        target = self.cfg["make_target"]
        print(f"    编译 {target}...")
        subprocess.run(["make", "clean"], cwd=self.project_dir,
                        capture_output=True)
        ret = subprocess.run(["make", target], cwd=self.project_dir,
                              capture_output=True, text=True)
        if ret.returncode != 0:
            print(f"    编译失败:\n{ret.stderr[-500:]}")
            raise RuntimeError(f"make {target} failed")

    def start(self):
        binary = os.path.join(self.project_dir, self.cfg["binary"])
        if not os.path.isfile(binary):
            self.build()
        binary = os.path.join(self.project_dir, self.cfg["binary"])
        if not os.path.isfile(binary):
            raise FileNotFoundError(f"二进制不存在: {binary}")

        env = os.environ.copy()
        env.update(self.cfg["env"])

        self.proc = subprocess.Popen(
            [binary, str(self.port)],
            cwd=self.project_dir,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        for _ in range(20):
            time.sleep(0.2)
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(1)
                s.connect(("127.0.0.1", self.port))
                s.close()
                return
            except Exception:
                continue
        raise RuntimeError(f"kvstore ({self.model}) 启动超时")

    def pid(self):
        return self.proc.pid if self.proc else None

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc = None


# ── 主测试流程 ────────────────────────────────────────────
def run_benchmark(model, engines, port, count, project_dir):
    """对指定网络模型和引擎列表运行完整基准测试"""
    print(f"\n{'='*70}")
    print(f"  网络模型: {model.upper()}  |  端口: {port}  |  每操作: {count} 次")
    print(f"{'='*70}")

    srv = ServerManager(project_dir, model, port)
    try:
        srv.build()
        srv.start()
    except FileNotFoundError as e:
        print(f"  SKIP: {e}")
        return None
    except RuntimeError as e:
        print(f"  ERROR: {e}")
        return None

    pid = srv.pid()
    clk_tck = get_clock_ticks()
    all_results = {}

    for eng_name in engines:
        ops = ENGINES[eng_name]
        print(f"\n  ── {eng_name.upper()} 引擎 {'─'*40}")

        client = KVClient("127.0.0.1", port)
        try:
            client.connect()
        except Exception as e:
            print(f"    连接失败: {e}")
            continue

        # 预热
        for i in range(100):
            client.cmd(f"{ops['SET']} warmup{i} val{i}")
        for i in range(100):
            client.cmd(f"{ops['DEL']} warmup{i}")

        # 采集 CPU/内存基线
        cpu_before = read_proc_stat(pid)
        mem_before = read_proc_memory(pid)
        wall_before = time.perf_counter()

        # 执行基准测试
        eng_results = bench_engine(client, eng_name, ops, count)

        # 采集 CPU/内存终值
        wall_after = time.perf_counter()
        cpu_after = read_proc_stat(pid)
        mem_after = read_proc_memory(pid)

        client.close()

        # 计算 CPU%
        wall_elapsed = wall_after - wall_before
        cpu_elapsed_sec = (cpu_after - cpu_before) / clk_tck
        cpu_pct = round(cpu_elapsed_sec / wall_elapsed * 100, 1) if wall_elapsed > 0 else 0

        total_ops = sum(r["count"] for r in eng_results.values())
        total_elapsed = sum(r["elapsed_ms"] for r in eng_results.values())
        overall_qps = int(total_ops / (total_elapsed / 1000)) if total_elapsed > 0 else 0

        summary = {
            "engine": eng_name,
            "network": model,
            "total_ops": total_ops,
            "total_elapsed_ms": round(total_elapsed, 1),
            "overall_qps": overall_qps,
            "cpu_pct": cpu_pct,
            "mem_rss_kb": mem_after,
            "mem_rss_mb": round(mem_after / 1024, 1),
            "operations": eng_results,
        }
        all_results[eng_name] = summary

        # 打印操作明细
        print(f"    {'操作':<6} {'QPS':>8} {'平均延迟':>10} {'P99延迟':>10} {'耗时(ms)':>10}")
        print(f"    {'─'*50}")
        for op_name in ["SET", "GET", "MOD", "EXIST", "DEL"]:
            r = eng_results[op_name]
            print(f"    {op_name:<6} {r['qps']:>8,} {r['avg_latency_us']:>8.1f}us {r['p99_latency_us']:>8.1f}us {r['elapsed_ms']:>10.1f}")
        print(f"    {'─'*50}")
        print(f"    总计   QPS: {overall_qps:,}  |  CPU: {cpu_pct}%  |  RSS: {summary['mem_rss_mb']}MB")

    srv.stop()
    return all_results


def print_engine_comparison(results_by_model):
    """打印引擎对比总表"""
    for model, results in results_by_model.items():
        if not results:
            continue
        print(f"\n{'='*70}")
        print(f"  引擎性能对比  [{model.upper()}]")
        print(f"{'='*70}")
        print(f"  {'引擎':<10} {'总QPS':>10} {'SET QPS':>10} {'GET QPS':>10} {'CPU%':>6} {'RSS(MB)':>8}")
        print(f"  {'─'*58}")
        for eng, data in results.items():
            ops = data["operations"]
            print(f"  {eng:<10} {data['overall_qps']:>10,} {ops['SET']['qps']:>10,} {ops['GET']['qps']:>10,} {data['cpu_pct']:>5.1f}% {data['mem_rss_mb']:>7.1f}")


def print_network_comparison(results_by_model):
    """打印网络框架对比表"""
    models = [m for m in results_by_model if results_by_model[m]]
    if len(models) < 2:
        return

    print(f"\n{'='*70}")
    print(f"  网络框架对比: {' vs '.join(m.upper() for m in models)}")
    print(f"{'='*70}")

    # 找出公共引擎
    common_engines = set.intersection(*(set(results_by_model[m].keys()) for m in models))

    print(f"  {'引擎':<10}", end="")
    for m in models:
        print(f"  {m.upper()+' QPS':>14}  {m.upper()+' CPU%':>10}", end="")
    print()
    print(f"  {'─'*66}")

    for eng in sorted(common_engines):
        print(f"  {eng:<10}", end="")
        for m in models:
            data = results_by_model[m][eng]
            print(f"  {data['overall_qps']:>14,}  {data['cpu_pct']:>9.1f}%", end="")
        print()


def save_report(results_by_model, output_dir):
    """保存 JSON 报告"""
    ts = time.strftime("%Y%m%d_%H%M%S")
    report = {
        "timestamp": ts,
        "results": results_by_model,
    }
    path = os.path.join(output_dir, f"benchmark_{ts}.json")
    with open(path, "w") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(f"\n  JSON 报告: {path}")
    return path


def main():
    parser = argparse.ArgumentParser(description="LiteMultiKV 核心性能基准测试")
    parser.add_argument("--port", type=int, default=18888)
    parser.add_argument("--count", type=int, default=10000, help="每引擎每操作的执行次数")
    parser.add_argument("--engine", choices=list(ENGINES.keys()) + ["all"], default="all")
    parser.add_argument("--network", choices=list(NETWORK_MODELS.keys()) + ["all"], default="reactor")
    args = parser.parse_args()

    project_dir = get_project_dir()
    output_dir = get_output_dir()
    engines = list(ENGINES.keys()) if args.engine == "all" else [args.engine]
    models = list(NETWORK_MODELS.keys()) if args.network == "all" else [args.network]

    print("=" * 70)
    print("  LiteMultiKV 核心性能基准测试")
    print(f"  时间:   {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  引擎:   {', '.join(engines)}")
    print(f"  网络:   {', '.join(models)}")
    print(f"  操作数: {args.count:,} / 引擎·操作")
    print("=" * 70)

    results_by_model = {}
    for model in models:
        results_by_model[model] = run_benchmark(
            model, engines, args.port, args.count, project_dir
        )

    # 汇总表
    print_engine_comparison(results_by_model)
    print_network_comparison(results_by_model)

    # 保存报告
    path = save_report(results_by_model, output_dir)

    print(f"\n{'='*70}")
    print("  基准测试完成")
    print(f"{'='*70}")


if __name__ == "__main__":
    main()
