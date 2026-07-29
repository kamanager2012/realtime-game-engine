#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/phase9/variance_engine.h"

namespace poker_engine {
namespace phase9 {

struct BatchAnalysisConfig {
  std::string input_directory;
  std::string hero_name_filter;
  int min_hands_for_analysis = 20;
  int session_group_size = 100;
  bool verbose = false;
};

struct PerPlayerAnalysis {
  std::string player_name;
  int64_t hands_analyzed = 0;
  double overall_bb100 = 0;
  double variance_bb100 = 0;
  double confidence_lower = 0;
  double confidence_upper = 0;
  double risk_of_ruin = 0;
  std::string classification;
  std::vector<std::pair<int, double>> bb100_over_time;
  std::string ToString() const;
};

class AdvancedBatchAnalyzer {
 public:
  explicit AdvancedBatchAnalyzer(const BatchAnalysisConfig& config);
  void Run();
  std::vector<PerPlayerAnalysis> GetPlayerAnalyses() const;
  std::string GetSummary() const;
  bool ExportJSON(const std::string& filepath);
  bool ExportHTML(const std::string& filepath);

 private:
  BatchAnalysisConfig config_;
  std::vector<PerPlayerAnalysis> analyses_;
  std::map<std::string, std::vector<poker_engine::phase4::HandHistory>> hands_by_player_;
};

}  // namespace phase9
}  // namespace poker_engine
