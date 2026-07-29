#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "poker_engine/phase4/hh_parser.h"

namespace poker_engine {
namespace phase5 {

struct ParseStats {
  int64_t total_files = 0;
  int64_t total_hands = 0;
  int64_t successful = 0;
  int64_t failed = 0;
  double elapsed_seconds = 0;
  double hands_per_second = 0;
  std::map<std::string, int64_t> sites;
  std::map<std::string, int64_t> game_types;
  std::map<int, int64_t> player_counts;

  std::string ToString() const;
};

struct HandWithMeta {
  poker_engine::phase4::HandHistory hh;
  std::string source_file;
  int64_t file_line = 0;
  bool parsed_ok = false;
  std::string error;
};

struct BulkParseConfig {
  int thread_count = 1;
  std::string file_extension = ".txt";
  bool quiet = false;
};

class BulkHandHistoryParser {
 public:
  BulkHandHistoryParser();
  explicit BulkHandHistoryParser(const BulkParseConfig& config);

  std::vector<poker_engine::phase4::HandHistory> ParseFile(const std::string& filepath);
  ParseStats ParseDirectory(const std::string& dir_path);
  ParseStats ParseDirectories(const std::vector<std::string>& dir_paths);

  const std::vector<HandWithMeta>& GetResults() const { return results_; }
  const ParseStats& GetStats() const { return stats_; }

  std::string ToJSON() const;
  bool ExportJSON(const std::string& filepath) const;

 private:
  BulkParseConfig config_;
  std::vector<HandWithMeta> results_;
  ParseStats stats_;
  std::mutex mutex_;

  void ParseFileInternal(const std::string& filepath);
  static std::vector<std::string> ListFiles(const std::string& dir_path,
                                            const std::string& extension);
};

class HandDatabase {
 public:
  void AddHand(const HandWithMeta& hand);
  void AddHands(const std::vector<HandWithMeta>& hands);
  void Clear();

  std::vector<HandWithMeta> GetHandsByPlayer(const std::string& player) const;
  std::vector<HandWithMeta> GetHandsBySite(const std::string& site) const;
  std::vector<HandWithMeta> GetHandsByBBRange(double min_bb, double max_bb) const;

  int64_t Count() const { return static_cast<int64_t>(hands_.size()); }

  struct AggregateStats {
    int64_t hand_count = 0;
    double avg_pot = 0;
    double avg_players = 0;
    std::map<std::string, int> action_distribution;
  };

  AggregateStats ComputeAggregate() const;

 private:
  std::vector<HandWithMeta> hands_;
  std::multimap<std::string, size_t> player_index_;
  std::multimap<std::string, size_t> site_index_;
};

}  // namespace phase5
}  // namespace poker_engine
