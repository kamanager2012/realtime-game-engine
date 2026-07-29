#pragma once
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/phase5/icm_calc.h"
#include "poker_engine/phase5/regression_analyzer.h"
#include "poker_engine/phase7/opponent_modeler.h"
#include "poker_engine/phase8/exploit_engine.h"
#include "poker_engine/phase9/polarized_range.h"
#include "poker_engine/phase9/strategy_diff.h"
#include "poker_engine/phase9/variance_engine.h"

namespace poker_engine {
namespace phase9 {

struct PipelineConfig {
  std::string data_source;
  std::string hero_filter;
  int min_hands = 10;
  bool run_variance = true;
  bool run_opponent_modeling = true;
  bool run_exploit_analysis = true;
  bool run_range_analysis = true;
  std::string output_path;
};

struct PipelineStageResult {
  std::string stage_name;
  bool success = false;
  std::string output;
  double processing_time_seconds = 0;
};

class AnalysisPipeline {
 public:
  explicit AnalysisPipeline(const PipelineConfig& config);

  PipelineStageResult Stage1_ParseHands();
  PipelineStageResult Stage2_VarianceAnalysis();
  PipelineStageResult Stage3_OpponentModeling();
  PipelineStageResult Stage4_ExploitAnalysis();
  PipelineStageResult Stage5_RangeAnalysis();
  PipelineStageResult Stage6_FinalReport();

  void RunFullPipeline();
  const std::vector<PipelineStageResult>& GetResults() const { return results_; }
  int HandCount() const { return static_cast<int>(hands_.size()); }

 private:
  PipelineConfig config_;
  std::vector<PipelineStageResult> results_;
  poker_engine::phase4::HandHistoryParser parser_;
  std::vector<poker_engine::phase4::HandHistory> hands_;
  poker_engine::phase7::OpponentModeler modeler_;
  poker_engine::phase8::ExploitEngine exploit_engine_;
};

class AutoReportGenerator {
 public:
  void SetTitle(const std::string& title);
  void AddSection(const std::string& heading, const std::string& content);
  std::string GenerateText() const;
  std::string GenerateHTML() const;

 private:
  std::string title_;
  std::vector<std::pair<std::string, std::string>> sections_;
};

}  // namespace phase9
}  // namespace poker_engine
