#include "poker_engine/phase9/advanced_batch_analyzer.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/phase4/hh_parser.h"

namespace poker_engine {
namespace phase9 {
using namespace poker_engine::phase4;

std::string PerPlayerAnalysis::ToString() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(2);
  o << player_name << " | " << hands_analyzed << " hands | " << overall_bb100 << " BB/100 | "
    << "CI:[" << confidence_lower << "," << confidence_upper << "] | RoR:" << risk_of_ruin * 100
    << "% | " << classification;
  return o.str();
}

AdvancedBatchAnalyzer::AdvancedBatchAnalyzer(const BatchAnalysisConfig& c) : config_(c) {}

void AdvancedBatchAnalyzer::Run() {
  analyses_.clear();
  hands_by_player_.clear();

  poker_engine::phase4::HandHistoryParser parser;
  auto all_hands = parser.ParseFromDirectory(config_.input_directory);

  if (config_.verbose) std::cout << "Parsed " << all_hands.size() << " hands\n";

  for (auto& hh : all_hands) {
    std::string hero = hh.HeroName();
    if (!config_.hero_name_filter.empty() && hero != config_.hero_name_filter) continue;
    hands_by_player_[hero].push_back(hh);
  }

  for (auto& [name, hands] : hands_by_player_) {
    if (static_cast<int>(hands.size()) < config_.min_hands_for_analysis) continue;
    PerPlayerAnalysis a;
    a.player_name = name;
    a.hands_analyzed = static_cast<int>(hands.size());

    VarianceEngine ve;
    for (size_t i = 0; i < hands.size(); i += config_.session_group_size) {
      double net = 0;
      size_t end = std::min(i + config_.session_group_size, hands.size());
      for (size_t j = i; j < end; j++)
        for (auto& r : hands[j].results)
          if (r.player_name == name || hands[j].HeroName() == name) net += r.amount;
      ve.AddBB100(net / std::max(1.0, (double)(end - i)) * 100);
    }

    auto ss = ve.ComputeSummary();
    a.overall_bb100 = ss.mean;
    a.variance_bb100 = ss.std_dev;
    a.confidence_lower = ss.ci_95_low;
    a.confidence_upper = ss.ci_95_high;
    a.risk_of_ruin = ve.RiskOfRuin(1000, 100);

    if (ss.mean > 5)
      a.classification = "Strong Winner";
    else if (ss.mean > 1)
      a.classification = "Winner";
    else if (ss.mean > -1)
      a.classification = "Breakeven";
    else if (ss.mean > -5)
      a.classification = "Loser";
    else
      a.classification = "Big Loser";

    auto wr = ve.RunningWinRate(20);
    a.bb100_over_time = wr;
    analyses_.push_back(a);
  }
  std::sort(analyses_.begin(), analyses_.end(),
            [](auto& a, auto& b) { return a.hands_analyzed > b.hands_analyzed; });
}

std::vector<PerPlayerAnalysis> AdvancedBatchAnalyzer::GetPlayerAnalyses() const {
  return analyses_;
}

std::string AdvancedBatchAnalyzer::GetSummary() const {
  std::ostringstream o;
  o << "=== Batch Analysis ===\nPlayers: " << analyses_.size() << "\n\n";
  for (auto& a : analyses_) o << a.ToString() << "\n";
  return o.str();
}

bool AdvancedBatchAnalyzer::ExportJSON(const std::string& fp) {
  std::ofstream out(fp);
  if (!out.is_open()) return false;
  out << std::fixed << std::setprecision(4);
  out << "{\"players\":[";
  for (size_t i = 0; i < analyses_.size(); i++) {
    auto& p = analyses_[i];
    out << "{\"name\":\"" << p.player_name << "\",\"hands\":" << p.hands_analyzed
        << ",\"bb100\":" << p.overall_bb100 << ",\"ci\":[" << p.confidence_lower << ","
        << p.confidence_upper << "],\"ror\":" << p.risk_of_ruin << ",\"class\":\""
        << p.classification << "\"}";
    if (i + 1 < analyses_.size()) out << ",";
  }
  out << "]}";
  out.close();
  return true;
}

bool AdvancedBatchAnalyzer::ExportHTML(const std::string& fp) {
  std::ofstream out(fp);
  if (!out.is_open()) return false;
  out << "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Batch</title>"
      << "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px;}"
      << "table{border-collapse:collapse;width:100%;}th,td{padding:8px;border-bottom:1px solid "
         "#333;}th{color:#06b6d4;}</style>"
      << "</head><body><h1>Batch Analysis</h1><table>"
      << "<tr><th>Player</th><th>Hands</th><th>BB/100</th><th>95% "
         "CI</th><th>RoR</th><th>Class</th></tr>";
  for (auto& p : analyses_) {
    out << "<tr><td>" << p.player_name << "</td><td>" << p.hands_analyzed << "</td><td>"
        << p.overall_bb100 << "</td><td>[" << p.confidence_lower << "," << p.confidence_upper << "]"
        << "</td><td>" << int(p.risk_of_ruin * 100) << "%</td><td>" << p.classification
        << "</td></tr>";
  }
  out << "</table></body></html>";
  out.close();
  return true;
}

}  // namespace phase9
}  // namespace poker_engine
