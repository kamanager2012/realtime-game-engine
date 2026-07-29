#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace poker_engine {
namespace phase5 {

// 回归分析结果
struct RegressionResult {
  double slope = 0;      // 趋势斜率
  double intercept = 0;  // 截距
  double r_squared = 0;  // R² 拟合优度
  double std_error = 0;  // 标准误差
  int n = 0;             // 数据点数
  double p_value = 1.0;  // 近似 p 值

  std::string Interpret() const;
  std::string ToString() const;
};

// 滑动窗口统计
struct WindowStats {
  double mean = 0;
  double variance = 0;
  double std_dev = 0;
  double min_val = 0;
  double max_val = 0;
  double median = 0;
  int count = 0;
  double sum = 0;

  double Confidence95() const;  // 95% 置信区间半径
  std::string ToString() const;
};

// 会话趋势点
struct TrendPoint {
  int hand_number;           // 手牌编号
  double bb_per_100;         // 累计 BB/100
  double running_bb_100;     // 滑动窗口 BB/100
  double cumulative_profit;  // 累计利润
};

class RegressionAnalyzer {
 public:
  RegressionAnalyzer();

  // 添加数据点
  void AddPoint(double x, double y);
  void AddProfit(double bb);  // 添加单手盈亏 (BB)

  // 线性回归
  RegressionResult LinearRegression() const;

  // 滑动窗口统计
  WindowStats SlidingWindow(int window_size) const;
  WindowStats OverallStats() const;

  // BB/100 趋势线
  std::vector<TrendPoint> ComputeTrend(int window_size = 100) const;

  // 异常检测 (偏离均值超过 N 个标准差)
  std::vector<int> DetectOutliers(double threshold_sigma = 2.0) const;

  // 运行热度 (最近 N 手的 BB/100 vs 总体)
  double HeatIndex(int recent_hands = 100) const;

  // 连胜/连败检测
  struct Streak {
    int start_idx;
    int length;
    double total_bb;
    bool is_winning;
  };
  std::vector<Streak> DetectStreaks() const;

  // 数据
  int Count() const { return static_cast<int>(x_vals_.size()); }
  void Clear();

  // 导出
  std::string TrendReport(int window_size = 100) const;

 private:
  std::vector<double> x_vals_;
  std::vector<double> y_vals_;
  std::vector<double> profits_;  // BB 序列

  // Welford's online algorithm state
  double welford_mean_ = 0;
  double welford_m2_ = 0;
  int welford_count_ = 0;
};

}  // namespace phase5
}  // namespace poker_engine
