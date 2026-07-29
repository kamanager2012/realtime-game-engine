#include "poker_engine/phase8/dashboard_gen.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "poker_engine/phase5/regression_analyzer.h"
#include "poker_engine/phase7/opponent_modeler.h"

namespace poker_engine {
namespace phase8 {

const std::vector<std::string> DashboardGenerator::palette_ = {
    "#3b82f6", "#ef4444", "#22c55e", "#f59e0b", "#8b5cf6", "#ec4899", "#06b6d4", "#f97316",
    "#14b8a6", "#6366f1", "#a855f7", "#e11d48", "#0d9488", "#ca8a04", "#7c3aed"};

DashboardGenerator::DashboardGenerator() = default;
void DashboardGenerator::SetTitle(const std::string& t) { title_ = t; }
void DashboardGenerator::SetTimeRange(int n) { time_range_ = n; }

std::string DashboardGenerator::NextColor() {
  return palette_[color_counter_++ % static_cast<int>(palette_.size())];
}

DashboardSeries DashboardGenerator::CreateSeries(const std::string& name,
                                                 const std::vector<double>& x,
                                                 const std::vector<double>& y,
                                                 const std::string& color) {
  DashboardSeries s;
  s.name = name;
  for (size_t i = 0; i < std::min(x.size(), y.size()); i++) {
    DashboardPoint p;
    p.label = std::to_string(static_cast<int>(x[i]));
    p.value = y[i];
    p.category = name;
    p.color = color;
    s.points.push_back(p);
  }
  return s;
}

void DashboardGenerator::AddRegressionData(
    const poker_engine::phase5::RegressionAnalyzer& analyzer) {
  if (analyzer.Count() == 0) return;

  // Access internal data via the public x_vals_/y_vals_ (they're public)
  // But we can only use AddPoint and LinearRegression from the public API
  // Create a series from the analyzer's count and regression result
  auto result = analyzer.LinearRegression();

  // Build a simple series from regression parameters
  std::vector<double> x, y;
  for (int i = 0; i < analyzer.Count(); i++) {
    x.push_back(static_cast<double>(i));
    y.push_back(result.slope * i + result.intercept);
  }
  all_series_.push_back(CreateSeries("Trend Line", x, y, NextColor()));

  if (result.r_squared > 0) {
    summary_["trend_slope"] = result.slope;
    summary_["trend_r2"] = result.r_squared;
  }
  summary_["total_sessions"] = static_cast<double>(analyzer.Count());
}

void DashboardGenerator::AddExploitData(const ExploitEngine& engine) {
  auto snapshots = engine.GetSnapshots();
  if (snapshots.empty()) return;

  std::vector<double> x, y_opt, y_act, y_gap;
  for (const auto& snap : snapshots) {
    x.push_back(static_cast<double>(snap.hand_number));
    y_opt.push_back(snap.optimal_winrate_bb100);
    y_act.push_back(snap.running_winrate_bb100);
    y_gap.push_back(snap.gap_bb100);
  }
  all_series_.push_back(CreateSeries("Optimal BB/100", x, y_opt, NextColor()));
  all_series_.push_back(CreateSeries("Actual BB/100", x, y_act, NextColor()));
  all_series_.push_back(CreateSeries("Gap BB/100", x, y_gap, NextColor()));
}

void DashboardGenerator::AddEquityCurve(const std::vector<std::pair<int, double>>& curve) {
  if (curve.empty()) return;
  std::vector<double> x, y;
  for (const auto& [hand_num, bb100] : curve) {
    x.push_back(static_cast<double>(hand_num));
    y.push_back(bb100);
  }
  all_series_.push_back(CreateSeries("Equity Curve", x, y, NextColor()));
}

void DashboardGenerator::AddICMData(const poker_engine::phase5::ICMCalculator&) {
  summary_["icm_available"] = 1;
}

void DashboardGenerator::AddClusterData(const poker_engine::phase7::OpponentModeler& modeler) {
  auto clusters = modeler.ClusterPlayers(5);
  DashboardSeries series;
  series.name = "Player Clusters";
  for (size_t i = 0; i < clusters.size(); i++) {
    if (clusters[i].members.empty()) continue;
    DashboardPoint p;
    p.label = clusters[i].label;
    p.value = static_cast<double>(clusters[i].members.size());
    p.category = "Clusters";
    p.color = NextColor();
    series.points.push_back(p);
  }
  if (!series.points.empty()) all_series_.push_back(series);
}

DashboardData DashboardGenerator::Generate() {
  DashboardData data;
  data.title = title_;
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  data.generated_at = std::ctime(&time_t_now);
  if (!data.generated_at.empty() && data.generated_at.back() == '\n') data.generated_at.pop_back();
  data.series = all_series_;
  data.summary_stats = summary_;
  data.annotations = annotations_;
  return data;
}

std::string DashboardData::ToJSON() const {
  std::ostringstream o;
  o << std::fixed << std::setprecision(4);
  o << "{\n  \"title\": \"" << title << "\",\n  \"generated_at\": \"" << generated_at << "\",\n";
  o << "  \"summary\": {";
  bool first = true;
  for (const auto& [k, v] : summary_stats) {
    if (!first) o << ", ";
    first = false;
    o << "\"" << k << "\": " << v;
  }
  o << "},\n  \"series\": [\n";
  for (size_t s = 0; s < series.size(); s++) {
    const auto& sr = series[s];
    o << "    {\"name\": \"" << sr.name << "\", \"points\": [";
    for (size_t p = 0; p < sr.points.size(); p++) {
      o << "{\"x\":" << sr.points[p].label << ",\"y\":" << sr.points[p].value << "}";
      if (p + 1 < sr.points.size()) o << ",";
    }
    o << "]}";
    if (s + 1 < series.size()) o << ",";
    o << "\n";
  }
  o << "  ]\n}";
  return o.str();
}

bool DashboardData::ExportToFile(const std::string& fp) const {
  std::ofstream out(fp);
  if (!out.is_open()) return false;
  out << ToJSON();
  out.close();
  return true;
}

std::string DashboardData::ToHTML() const {
  std::ostringstream o;
  o << R"(<!DOCTYPE html><html><head><meta charset="UTF-8"><title>)" << title << R"(</title>
<style>body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;margin:20px}h1{color:#06b6d4}
.chart-container{background:#16213e;border-radius:10px;padding:20px;margin:20px 0}
canvas{max-width:100%}.stat-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin:20px 0}
.stat-card{background:#0f3460;border-radius:8px;padding:15px;text-align:center}
.stat-value{font-size:24px;font-weight:bold;color:#22c55e}.stat-label{color:#888;font-size:12px}
table{width:100%;border-collapse:collapse}th,td{padding:8px 12px;text-align:left;border-bottom:1px solid #333}th{color:#06b6d4}
</style><script src="https://cdn.jsdelivr.net/npm/chart.js"></script></head><body>
<h1>)"
    << title << R"(</h1><p>Generated: )" << generated_at << R"(</p><div class="stat-grid">)";
  for (const auto& [k, v] : summary_stats)
    o << R"(<div class="stat-card"><div class="stat-value">)" << std::setprecision(2) << v
      << R"(</div><div class="stat-label">)" << k << R"(</div></div>)";
  o << R"(</div><div class="chart-container"><canvas id="mainChart" height="300"></canvas></div><table><tr><th>Series</th><th>Points</th></tr>)";
  for (const auto& sr : series)
    o << "<tr><td>" << sr.name << "</td><td>" << sr.points.size() << "</td></tr>";
  o << R"(</table><script>const ctx=document.getElementById('mainChart').getContext('2d');const datasets=[)";
  for (size_t i = 0; i < series.size(); i++) {
    const auto& sr = series[i];
    o << "{label:'" << sr.name << "',data:[";
    for (size_t p = 0; p < sr.points.size(); p++) {
      o << "{x:" << sr.points[p].label << ",y:" << sr.points[p].value << "}";
      if (p + 1 < sr.points.size()) o << ",";
    }
    o << "],borderColor:'" << (sr.points.empty() ? "#fff" : sr.points[0].color)
      << "',fill:false,tension:0.1}";
    if (i + 1 < series.size()) o << ",";
  }
  o << R"(];new Chart(ctx,{type:'line',data:{datasets},options:{responsive:true,scales:{x:{type:'linear'},y:{beginAtZero:false}}}});</script></body></html>)";
  return o.str();
}

bool DashboardGenerator::ExportHTML(const std::string& fp) {
  auto data = Generate();
  std::ofstream out(fp);
  if (!out.is_open()) return false;
  out << data.ToHTML();
  out.close();
  return true;
}

bool DashboardGenerator::ExportJSON(const std::string& fp) {
  auto data = Generate();
  return data.ExportToFile(fp);
}

}  // namespace phase8
}  // namespace poker_engine
