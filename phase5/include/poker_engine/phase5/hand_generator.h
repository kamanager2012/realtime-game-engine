#pragma once
#include <map>
#include <random>
#include <string>
#include <vector>

#include "poker_engine/evaluator/evaluator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase5 {

// 手牌生成配置
struct HandGenConfig {
  int num_hands = 1000;
  int num_players = 6;
  double big_blind = 1.0;
  double ante = 0.0;
  bool hero_position_fixed = false;
  int hero_position = 0;  // 0=SB, 5=BTN in 6-max
};

// 单手生成结果
struct GeneratedHand {
  std::vector<std::string> player_names;
  std::vector<std::string> hole_cards;  // 每人底牌字符串
  std::string board;                    // 公共牌
  std::vector<double> equities;         // 胜率
  std::vector<double> stacks;           // 筹码
  int button_pos = 0;
};

// 手牌分布统计
struct HandDistribution {
  int total_generated = 0;
  std::map<std::string, int> hand_counts;          // "AA" → 出现次数
  std::map<std::string, double> hand_frequencies;  // "AA" → 频率

  std::string ToString() const;
};

class HandGenerator {
 public:
  explicit HandGenerator(uint32_t seed = 42);

  // 从范围生成随机手牌
  std::pair<poker_engine::Card, poker_engine::Card> GenerateHand(
      const poker_engine::range::Range& range);

  // 生成完整牌局
  GeneratedHand GenerateFullHand(const HandGenConfig& config);

  // 批量生成
  std::vector<GeneratedHand> GenerateBatch(int num_hands, const HandGenConfig& config);

  // 统计手牌分布
  HandDistribution ComputeDistribution(int num_samples, int num_players = 6);

  // 位置范围表 (6-max 标准范围)
  static const char* PositionName(int pos);
  static poker_engine::range::Range GetPositionRange(int pos);

 private:
  std::mt19937 rng_;
  std::vector<uint8_t> deck_;

  void ShuffleDeck();
  std::pair<uint8_t, uint8_t> DealTwoCards();
  std::vector<uint8_t> DealBoard(int num_cards, const std::vector<uint8_t>& exclude);
};

}  // namespace phase5
}  // namespace poker_engine
