#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace poker_engine {
namespace phase5 {

struct ICMResult {
  double equity[10];         // 每位玩家的 ICM 权益 ($)
  double bubble_factor[10];  // 气泡因子
  double m_zone[10];         // M 值 (筹码/盲注)
  double risk_premium[10];   // 风险溢价

  std::string ToString() const;
};

struct PushFoldEntry {
  int hand_rank;             // 169 抽象手牌编号
  std::string hand_name;     // "AA", "AKs" 等
  bool push = false;         // 是否应该推全下
  double ev_push = 0;        // 推全下的 EV
  double ev_fold = 0;        // 弃牌的 EV
  double equity_needed = 0;  // 所需胜率
};

class ICMCalculator {
 public:
  // 核心 ICM 计算
  static ICMResult Calculate(const double payouts[], int payout_count, const double chips[],
                             int player_count, double big_blind = 1.0);

  // 气泡因子
  static double BubbleFactor(const double payouts[], int payout_count, const double chips[],
                             int player_count, int player_idx);

  // M 值
  static double MValue(double stack, double big_blind, double ante = 0, int players = 6);

  // 风险溢价
  static double RiskPremium(const double payouts[], int payout_count, const double chips[],
                            int player_count, int player_idx);

  // Push/Fold 表 (简化: 2 人)
  static std::vector<PushFoldEntry> PushFoldTable(double hero_stack_bb, double effective_bb);

  // ICM 压力指数 (0~1, 1=最大压力)
  static double PressureIndex(const double payouts[], int payout_count, const double chips[],
                              int player_count, int player_idx);

 private:
  static void ICMRecurse(double equity[], const double remaining[], int n, const double payouts[],
                         int payout_count, int place, double prob);
};

}  // namespace phase5
}  // namespace poker_engine
