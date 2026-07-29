#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase3 {

// 批量模拟配置
struct BatchConfig {
  int iterations = 10000;
  int hands_per_iter = 2;
  double initial_stack = 100.0;
  double big_blind = 1.0;
  double ante = 0.0;
  bool verbose = false;
};

// 单次模拟结果
struct SimulationResult {
  int64_t hand_id;
  std::vector<std::string> player_names;
  std::vector<double> final_stacks;
  std::vector<double> starting_stacks;
  std::vector<std::string> hero_cards_list;
  std::string board_str;
  std::vector<double> equities;
  std::string winner;
  double pot_size;
};

// 批量跑牌模拟器
class BatchSimulator {
 public:
  explicit BatchSimulator(const BatchConfig& config);

  // 添加玩家范围
  void AddPlayer(const std::string& name, const std::string& range_str);
  void AddPlayer(const std::string& name, const poker_engine::range::Range& range);

  // 运行批量模拟
  std::vector<SimulationResult> Run(int num_hands);

  // 统计报表
  struct PlayerStats {
    std::string name;
    int hands_played = 0;
    double total_won = 0;
    double total_invested = 0;
    double roi() const { return total_invested > 0 ? total_won / total_invested : 0; }
    double avg_equity = 0;
    int wins = 0;
  };

  std::map<std::string, PlayerStats> GetStats() const;
  std::string StatsReport() const;

  // 回调 (每手牌结束后调用)
  void SetCallback(std::function<void(const SimulationResult&)> cb);

 private:
  BatchConfig config_;
  std::vector<std::pair<std::string, poker_engine::range::Range>> players_;
  std::map<std::string, PlayerStats> stats_;
  std::function<void(const SimulationResult&)> callback_;
  std::mt19937 rng_{42};
  int64_t hand_counter_ = 0;

  // 内部方法
  void DealCards(std::vector<uint8_t>& hero_cards_out);
  std::vector<Card> DealBoard(int num_cards, const std::vector<uint8_t>& exclude);
  std::string CardsToString(const std::vector<Card>& cards) const;
};

}  // namespace phase3
}  // namespace poker_engine
