#pragma once
#include <chrono>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "poker_engine/phase5/icm_calc.h"
#include "poker_engine/phase5/regression_analyzer.h"
#include "poker_engine/phase7/opponent_modeler.h"
#include "poker_engine/phase8/exploit_engine.h"

namespace poker_engine {
namespace phase8 {

struct DashboardPoint {
  std::string label;
  double value;
  std::string category;
  std::string color;
};

struct DashboardSeries {
  std::string name;
  std::vector<DashboardPoint> points;
};

struct DashboardData {
  std::string title;
  std::string generated_at;
  std::vector<DashboardSeries> series;
  std::vector<std::string> annotations;
  std::map<std::string, double> summary_stats;

  std::string ToJSON() const;
  bool ExportToFile(const std::string& filepath) const;
  std::string ToHTML() const;
};

class DashboardGenerator {
 public:
  DashboardGenerator();

  void AddRegressionData(const poker_engine::phase5::RegressionAnalyzer& analyzer);
  void AddExploitData(const poker_engine::phase8::ExploitEngine& engine);
  void AddEquityCurve(const std::vector<std::pair<int, double>>& curve);
  void AddICMData(const poker_engine::phase5::ICMCalculator& icm);
  void AddClusterData(const poker_engine::phase7::OpponentModeler& modeler);

  void SetTitle(const std::string& title);
  void SetTimeRange(int last_n_hands);

  DashboardData Generate();
  bool ExportHTML(const std::string& filepath);
  bool ExportJSON(const std::string& filepath);

 private:
  std::string title_ = "Poker Engine Dashboard";
  int time_range_ = 0;
  std::vector<DashboardSeries> all_series_;
  std::map<std::string, double> summary_;
  std::vector<std::string> annotations_;
  int color_counter_ = 0;

  static const std::vector<std::string> palette_;
  std::string NextColor();

  DashboardSeries CreateSeries(const std::string& name, const std::vector<double>& x,
                               const std::vector<double>& y, const std::string& color);
};

}  // namespace phase8
}  // namespace poker_engine
