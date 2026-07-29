#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace poker_engine::serialization {

struct SerializationBenchmark {
  static constexpr int kIterations = 1000000;

  struct BenchmarkResult {
    std::string name;
    double avg_ns;
    double throughput_mbs;
    size_t avg_bytes;
  };

  // 运行 Ping-Pong 基准测试
  static BenchmarkResult Run(const std::string& label,
                             void (*serialize_fn)(void*& data, size_t& size),
                             void (*deserialize_fn)(const void* data, size_t size)) {
    // Warmup
    void* data = nullptr;
    size_t size = 0;
    for (int i = 0; i < 100; ++i) {
      serialize_fn(data, size);
      deserialize_fn(data, size);
    }

    // Measure serialize
    auto s_start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
      serialize_fn(data, size);
    }
    auto s_end = std::chrono::steady_clock::now();
    auto serialize_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(s_end - s_start).count() / kIterations;

    // Measure deserialize
    auto d_start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
      deserialize_fn(data, size);
    }
    auto d_end = std::chrono::steady_clock::now();
    auto deserialize_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(d_end - d_start).count() / kIterations;

    BenchmarkResult result;
    result.name = label;
    result.avg_ns = serialize_ns + deserialize_ns;
    result.avg_bytes = size;
    result.throughput_mbs =
        (size * 2.0 * kIterations / (serialize_ns + deserialize_ns)) / 1e6;  // rough MB/s

    return result;
  }

  static void PrintComparison(const BenchmarkResult& json_result,
                              const BenchmarkResult& flat_result) {
    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║        Serialization Benchmark Results                 ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║                          JSON      FlatBuffers          ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Avg Time (ns)      %10.0f  %10.0f           ║\n", json_result.avg_ns,
           flat_result.avg_ns);
    printf("║ Throughput (MB/s)  %10.2f  %10.2f          ║\n", json_result.throughput_mbs,
           flat_result.throughput_mbs);
    printf("║ Size (bytes)        %10zu  %10zu            ║\n", json_result.avg_bytes,
           flat_result.avg_bytes);
    printf("║ Speedup              %10.1fx  %10.1fx         ║\n", 1.0,
           json_result.avg_ns / flat_result.avg_ns);
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("FlatBuffers %.1fx faster, %.1fx smaller\n", json_result.avg_ns / flat_result.avg_ns,
           static_cast<double>(json_result.avg_bytes) / flat_result.avg_bytes);
  }
};

}  // namespace poker_engine::serialization
