#!/bin/bash
# run_benchmark.sh - LiteMultiKV 一键性能基准测试
#
# 清理旧进程 → 运行基准测试（自动编译） → 输出对比报告
#
# 用法:
#   ./run_benchmark.sh                     # 默认: reactor, 全引擎, 10000次/操作
#   ./run_benchmark.sh --network all       # reactor + proactor 对比
#   ./run_benchmark.sh --count 50000       # 大规模测试
#   ./run_benchmark.sh --engine hash       # 单引擎测试
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "================================================================"
echo "  LiteMultiKV 性能基准测试"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "================================================================"
echo ""

# 清理旧进程
pkill -x kvstore 2>/dev/null || true
pkill -x kvstore_proactor 2>/dev/null || true
sleep 1

# 运行基准测试（benchmark.py 自动处理编译）
python3 "${SCRIPT_DIR}/benchmark.py" "$@"
