#pragma once
#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase10/parallel_utils.h"

namespace poker_engine {
namespace phase10 {

struct ParallelParseStats {
  int64_t total_files = 0, total_hands = 0, successful = 0, failed = 0;
  double elapsed_seconds = 0, hands_per_second = 0;
  std::vector<std::string> failed_files;
  std::string ToString() const;
};

struct ParallelParseConfig {
  int num_threads = -1;
  std::string file_extension = ".txt";
  bool recursive = true;
  int batch_size = 50;
  bool verbose = false;
};

struct ParsedHandResult {
  std::string source_file;
  bool success = false;
  std::string error, hero_name, site, summary;
  int hand_id = 0;
  double big_blind = 0, total_pot = 0;
};

class ParallelBatchParser {
 public:
  explicit ParallelBatchParser(const ParallelParseConfig& config = ParallelParseConfig());
  ParallelParseStats ParseDirectory(const std::string& dir_path);
  const std::vector<ParsedHandResult>& GetResults() const { return results_; }
  const ParallelParseStats& GetStats() const { return stats_; }
  ParallelParseStats QuickCount(const std::string& dir_path);

 private:
  ParallelParseConfig config_;
  std::vector<ParsedHandResult> results_;
  ParallelParseStats stats_;
  std::mutex results_mutex_, stats_mutex_;
  static std::vector<std::string> ListFiles(const std::string& dir, const std::string& ext,
                                            bool recursive);
};

}  // namespace phase10
}  // namespace poker_engine
