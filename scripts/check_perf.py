#!/usr/bin/env python3
"""
性能门禁检查脚本
读取 Google Benchmark JSON 输出，检查各项指标是否在允许范围内
失败时 exit 1，阻塞 CI
"""

import json
import sys
import os

# 性能限制定义
# 来源：ADR-004 + 实际压测基线
PERF_LIMITS = {
    "BM_AI_Decision/p99":              100_000,   # 100ms
    "BM_AI_Decision/mean":             10_000,    # 10ms 均值
    "BM_ActionValidator_Validate/mean": 1_000,    # 1µs 均值 (1000ns)
    "BM_ActionValidator_Validate/p50":  500,       # 500ns p50
    "BM_Serialization_RoundTrip/p99":   1_000,    # 1ms
    "BM_Serialization_RoundTrip/mean":  100,       # 100µs
    "BM_Session_Create/mean":           1_000,     # 1µs
    "BM_Table_StartHand/mean":          5_000,     # 5µs
}

def parse_benchmark_name(full_name):
    if full_name.startswith("*"):
        return full_name[1:]
    return full_name

def check_performance(report_path):
    if not os.path.exists(report_path):
        print(f"❌ 性能报告文件不存在: {report_path}")
        return False

    with open(report_path) as f:
        data = json.load(f)

    benchmarks = data.get("benchmarks", [])
    failures = []
    warnings = []

    for bench in benchmarks:
        full_name = bench.get("name", "")
        name = parse_benchmark_name(full_name)

        for suffix in ["p99", "p95", "mean", "real_accumulated_time"]:
            if suffix in name:
                key = name
                break
        else:
            key = name + "/mean"

        if key not in PERF_LIMITS:
            continue

        limit = PERF_LIMITS[key]

        actual = bench.get("real_accumulated_time", 0)
        if actual == 0:
            actual = bench.get("cpu_time", 0)

        stats = bench.get("stats", {})
        mean = stats.get("mean", actual)

        actual_us = actual
        limit_us = limit

        passed = actual_us <= limit_us
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {status} {key}: actual={actual_us:.1f}µs, limit={limit_us}µs")

        if not passed:
            failures.append(f"{key}: {actual_us:.1f}µs > {limit_us}µs limit")
        elif actual_us > limit_us * 0.8:
            warnings.append(f"{key}: {actual_us:.1f}µs (80% of {limit_us}µs limit)")

    print()

    if failures:
        print("=" * 60)
        print("❌ 性能门禁 FAILED")
        print("=" * 60)
        for f in failures:
            print(f"  ❌ {f}")
        return False

    if warnings:
        print("⚠️  性能接近限制:")
        for w in warnings:
            print(f"  ⚠️  {w}")

    print("✅ 所有性能指标通过")
    return True


if __name__ == "__main__":
    report = sys.argv[1] if len(sys.argv) > 1 else "perf_report.json"
    success = check_performance(report)
    sys.exit(0 if success else 1)
