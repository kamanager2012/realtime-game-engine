#include "poker_engine/phase9/pipeline_orchestrator.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

namespace poker_engine {
namespace phase9 {
using namespace poker_engine::phase4;
using namespace poker_engine::phase7;
using namespace poker_engine::phase8;

static double now_sec() {
  return std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

AnalysisPipeline::AnalysisPipeline(const PipelineConfig& c) : config_(c) {}

PipelineStageResult StageResult(const std::string& name, bool ok, const std::string& out,
                                double t0) {
  PipelineStageResult r;
  r.stage_name = name;
  r.success = ok;
  r.output = out;
  r.processing_time_seconds = now_sec() - t0;
  return r;
}

PipelineStageResult AnalysisPipeline::Stage1_ParseHands() {
  double t0 = now_sec();
  if (config_.data_source.empty()) return StageResult("Parse", false, "No data source", t0);

  bool is_dir = config_.data_source.find(".txt") == std::string::npos;
  if (is_dir)
    hands_ = parser_.ParseFromDirectory(config_.data_source);
  else {
    auto r = parser_.ParseFromFile(config_.data_source);
    if (r.parsed_ok) hands_.push_back(r.hh);
  }

  if (!config_.hero_filter.empty() && config_.hero_filter != "*") {
    std::vector<HandHistory> f;
    for (auto& hh : hands_)
      if (hh.HeroName().find(config_.hero_filter) != std::string::npos) f.push_back(hh);
    hands_ = f;
  }

  std::ostringstream o;
  o << "Parsed: " << hands_.size() << " hands from " << config_.data_source << "\n";
  if (!hands_.empty())
    o << "First: #" << hands_[0].hand_id << " Last: #" << hands_.back().hand_id << "\n";
  return StageResult("Parse", !hands_.empty(), o.str(), t0);
}

PipelineStageResult AnalysisPipeline::Stage2_VarianceAnalysis() {
  double t0 = now_sec();
  if (hands_.empty()) return StageResult("Variance", false, "No hands", t0);

  VarianceEngine ve;
  for (size_t i = 0; i < hands_.size(); i += 100) {
    double net = 0;
    size_t end = std::min(i + 100, hands_.size());
    for (size_t j = i; j < end; j++)
      for (auto& r : hands_[j].results)
        if (r.player_name == hands_[j].HeroName()) net += r.amount;
    ve.AddBB100(net / std::max(1.0, (double)(end - i)) * 100);
  }

  std::ostringstream o;
  o << ve.ComputeSummary().ToString() << "\n"
    << ve.ComputeSWRR().ToString() << "\n"
    << ve.ComputeHeatIndex().ToString();
  return StageResult("Variance", true, o.str(), t0);
}

PipelineStageResult AnalysisPipeline::Stage3_OpponentModeling() {
  double t0 = now_sec();
  if (hands_.empty()) return StageResult("OppModel", false, "No hands", t0);
  modeler_.ProcessHands(hands_);

  std::ostringstream o;
  o << "Players: " << modeler_.PlayerCount() << " | Hands: " << modeler_.TotalHandsProcessed()
    << "\n";
  if (modeler_.PlayerCount() > 0)
    for (auto& [n, s] : modeler_.AllStats())
      if (s.hands_seen >= 5) {
        o << modeler_.PlayerReport(n);
        break;
      }
  return StageResult("OppModel", true, o.str(), t0);
}

PipelineStageResult AnalysisPipeline::Stage4_ExploitAnalysis() {
  double t0 = now_sec();
  if (hands_.empty()) return StageResult("Exploit", false, "No hands", t0);
  exploit_engine_.SetBaselineRanges(poker_engine::range::Range::FullCombinatorial(),
                                    poker_engine::range::Range::FullCombinatorial());
  auto s = exploit_engine_.AnalyzeHandHistory(hands_, config_.hero_filter);
  return StageResult("Exploit", true, s.ToString(), t0);
}

PipelineStageResult AnalysisPipeline::Stage5_RangeAnalysis() {
  double t0 = now_sec();
  std::ostringstream o;
  o << "Range analysis: " << hands_.size() << " hands\n";

  auto base = poker_engine::range::Range::FromString("22+,A2s+,K2s+");
  std::vector<poker_engine::Card> board;
  if (!hands_.empty() && !hands_[0].all_board_cards.empty()) board = hands_[0].all_board_cards;
  auto pr = PolarizedRangeBuilder::BuildPolarized(base, board);
  o << pr.ToString();
  return StageResult("Range", true, o.str(), t0);
}

PipelineStageResult AnalysisPipeline::Stage6_FinalReport() {
  double t0 = now_sec();
  std::ostringstream o;
  o << "===== FULL ANALYSIS REPORT =====\n";
  o << "Hands: " << hands_.size()
    << " | Hero: " << (config_.hero_filter.empty() ? "all" : config_.hero_filter) << "\n\n";
  for (auto& r : results_)
    o << "--- " << r.stage_name << " [" << r.processing_time_seconds << "s] ---\n"
      << r.output << "\n";
  if (!config_.output_path.empty()) {
    std::ofstream f(config_.output_path);
    if (f.is_open()) {
      f << o.str();
      f.close();
      o << "Saved: " << config_.output_path << "\n";
    }
  }
  return StageResult("Report", true, o.str(), t0);
}

void AnalysisPipeline::RunFullPipeline() {
  auto r1 = Stage1_ParseHands();
  results_.push_back(r1);
  if (!r1.success || hands_.size() < static_cast<size_t>(config_.min_hands)) return;

  if (config_.run_variance) results_.push_back(Stage2_VarianceAnalysis());
  if (config_.run_opponent_modeling) results_.push_back(Stage3_OpponentModeling());
  if (config_.run_exploit_analysis) results_.push_back(Stage4_ExploitAnalysis());
  if (config_.run_range_analysis) results_.push_back(Stage5_RangeAnalysis());
  results_.push_back(Stage6_FinalReport());

  for (auto& r : results_)
    std::cout << (r.success ? "[OK]" : "[SKIP]") << " " << r.stage_name << " ("
              << r.processing_time_seconds << "s)\n";
}

void AutoReportGenerator::SetTitle(const std::string& t) { title_ = t; }
void AutoReportGenerator::AddSection(const std::string& h, const std::string& c) {
  sections_.push_back({h, c});
}

std::string AutoReportGenerator::GenerateText() const {
  std::ostringstream o;
  o << title_ << "\n" << std::string(title_.size(), '=') << "\n\n";
  for (auto& [h, c] : sections_) o << "## " << h << "\n\n" << c << "\n\n";
  return o.str();
}

std::string AutoReportGenerator::GenerateHTML() const {
  std::ostringstream o;
  o << "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>" << title_
    << "</"
       "title><style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px;}h2{"
       "color:#06b6d4;}pre{background:#16213e;padding:10px;border-radius:5px;}</style></"
       "head><body><h1>"
    << title_ << "</h1>\n";
  for (auto& [h, c] : sections_) o << "<h2>" << h << "</h2><pre>" << c << "</pre>\n";
  o << "</body></html>";
  return o.str();
}

}  // namespace phase9
}  // namespace poker_engine
