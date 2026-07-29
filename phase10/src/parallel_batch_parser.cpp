#include "poker_engine/phase10/parallel_batch_parser.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/phase4/hh_parser.h"

namespace poker_engine {
namespace phase10 {
using phase4::HandHistoryParser;
using phase4::SourcedHandHistory;

std::string ParallelParseStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "=== Parallel Parse Stats ===\n"
      << "Files: " << total_files << " | Hands: " << total_hands << " | OK: " << successful
      << " | Fail: " << failed << " | Time: " << elapsed_seconds
      << "s | Speed: " << hands_per_second << " hands/s\n";
  return oss.str();
}

ParallelBatchParser::ParallelBatchParser(const ParallelParseConfig& config) : config_(config) {}

std::vector<std::string> ParallelBatchParser::ListFiles(const std::string& dir,
                                                        const std::string& ext, bool recursive) {
  std::vector<std::string> files;
  if (!std::filesystem::exists(dir)) return files;
  if (recursive) {
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir))
      if (e.is_regular_file() && e.path().extension() == ext) files.push_back(e.path().string());
  } else {
    for (const auto& e : std::filesystem::directory_iterator(dir))
      if (e.is_regular_file() && e.path().extension() == ext) files.push_back(e.path().string());
  }
  return files;
}

ParallelParseStats ParallelBatchParser::ParseDirectory(const std::string& dir_path) {
  auto start = std::chrono::steady_clock::now();
  results_.clear();
  stats_ = ParallelParseStats();

  auto files = ListFiles(dir_path, config_.file_extension, config_.recursive);
  stats_.total_files = static_cast<int64_t>(files.size());

  ThreadPool pool(config_.num_threads);
  for (const auto& f : files) {
    pool.Submit([&, f]() {
      HandHistoryParser parser;
      auto result = parser.ParseFromFile(f);
      ParsedHandResult pres;
      pres.source_file = f;
      pres.success = result.parsed_ok;
      if (result.parsed_ok) {
        pres.hand_id = result.hh.hand_id;
        pres.hero_name = result.hh.HeroName();
        pres.total_pot = result.hh.total_pot;
      } else {
        pres.error = result.error_msg;
      }
      {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.push_back(pres);
      }
      {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (result.parsed_ok) {
          stats_.successful++;
          stats_.total_hands++;
        } else {
          stats_.failed++;
          stats_.failed_files.push_back(f);
        }
      }
    });
  }
  pool.WaitAll();

  auto end = std::chrono::steady_clock::now();
  stats_.elapsed_seconds = std::chrono::duration<double>(end - start).count();
  stats_.hands_per_second =
      stats_.elapsed_seconds > 0 ? stats_.total_hands / stats_.elapsed_seconds : 0;
  return stats_;
}

ParallelParseStats ParallelBatchParser::QuickCount(const std::string& dir_path) {
  ParallelParseStats stats;
  auto files = ListFiles(dir_path, config_.file_extension, config_.recursive);
  stats.total_files = static_cast<int64_t>(files.size());

  std::atomic<int64_t> total_hands{0};
  ThreadPool pool(config_.num_threads);
  for (const auto& f : files) {
    pool.Submit([&, f]() {
      std::ifstream file(f);
      int count = 0;
      std::string line;
      while (std::getline(file, line))
        if (line.find("PokerStars Hand #") != std::string::npos) count++;
      total_hands += count;
    });
  }
  pool.WaitAll();
  stats.total_hands = total_hands.load();
  return stats;
}

}  // namespace phase10
}  // namespace poker_engine
