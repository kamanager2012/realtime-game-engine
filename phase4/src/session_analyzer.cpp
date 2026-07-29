#include "poker_engine/phase4/session_analyzer.h"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "poker_engine/phase4/hh_parser.h"

namespace poker_engine {
namespace phase4 {

using namespace poker_engine::phase4;

void SessionAnalyzer::AddHand(const HandHistory& hh) { hands_.push_back(hh); }

void SessionAnalyzer::LoadDirectory(const std::string& dir_path) {
  HandHistoryParser parser;
  auto parsed = parser.ParseFromDirectory(dir_path);
  for (auto& hh : parsed) hands_.push_back(hh);
}

SessionStats SessionAnalyzer::ComputeStats() const {
  SessionStats stats;
  stats.total_hands = static_cast<int>(hands_.size());

  double total_profit = 0;
  double total_buyins = 0;
  double total_rake = 0;

  for (const auto& hh : hands_) {
    // 计算 Hero 本手利润
    double hero_profit = 0;
    std::string hero_name = hh.HeroName();

    for (const auto& r : hh.results) {
      if (r.player_name == hero_name) hero_profit += r.amount;
    }

    // 总投入
    double invested = 0;
    for (const auto& st : hh.streets) {
      for (const auto& a : st.actions) {
        if (a.player_name == hero_name) {
          if (a.action == ActionType::POST_SB || a.action == ActionType::POST_BB ||
              a.action == ActionType::POST_ANTE || a.action == ActionType::CALL ||
              a.action == ActionType::BET || a.action == ActionType::RAISE ||
              a.action == ActionType::ALL_IN)
            invested += a.amount;
        }
      }
    }

    // 如果是赢家，profit 已经包含了赢的钱; 需要减去投入
    double net = hero_profit - invested;
    total_profit += net;
    total_buyins += hh.big_blind * 100;  // 简化: 每手买 100BB
    total_rake += hh.total_pot * 0.05;   // 简化: 5% rake

    // VPIP/PFR by position
    // 简化: 标记每个座位
    for (size_t i = 0; i < hh.seats.size(); i++) {
      const auto& seat = hh.seats[i];
      std::string pos_name = "Seat" + std::to_string(seat.seat_no);
      if (seat.seat_no == hh.button_seat)
        pos_name = "BTN";
      else if (i == 0)
        pos_name = "SB";
      else if (i == 1)
        pos_name = "BB";
      else if (i == 2)
        pos_name = "UTG";
      else if (i == 3)
        pos_name = "MP";
      else if (i == hh.seats.size() - 1)
        pos_name = "CO";

      bool voluntarily_put_money = false;
      bool preflop_raise = false;

      for (const auto& st : hh.streets) {
        if (static_cast<int>(st.street) > 0) break;  // 只看 preflop
        for (const auto& a : st.actions) {
          if (a.player_name != seat.player_name) continue;
          if (a.action == ActionType::CALL || a.action == ActionType::BET ||
              a.action == ActionType::RAISE || a.action == ActionType::ALL_IN ||
              a.action == ActionType::POST_SB || a.action == ActionType::POST_BB)
            voluntarily_put_money = true;
          if (a.action == ActionType::RAISE || a.action == ActionType::ALL_IN) preflop_raise = true;
        }
      }

      if (voluntarily_put_money) stats.vpip_by_pos[pos_name]++;
      if (preflop_raise) stats.pfr_by_pos[pos_name]++;
    }
  }

  stats.total_net = total_profit;
  stats.total_buyins = total_buyins;
  stats.total_rake = total_rake;
  stats.bb_per_100 = (total_profit / std::max(1.0, total_buyins)) * 100.0;
  stats.agg_factor = 0.0;  // 简化
  stats.wt_sd = 0.0;

  return stats;
}

std::vector<HandHistory> SessionAnalyzer::FilterByPlayer(const std::string& player) const {
  std::vector<HandHistory> result;
  for (const auto& hh : hands_) {
    if (hh.HeroName() == player || hh.seat_map.count(player)) result.push_back(hh);
  }
  return result;
}

std::vector<HandHistory> SessionAnalyzer::FilterByGameType(const std::string& gt) const {
  std::vector<HandHistory> result;
  for (const auto& hh : hands_) {
    if (hh.game_type.find(gt) != std::string::npos) result.push_back(hh);
  }
  return result;
}

std::vector<HandHistory> SessionAnalyzer::FilterByBBRange(double min_bb, double max_bb) const {
  std::vector<HandHistory> result;
  for (const auto& hh : hands_) {
    if (hh.big_blind >= min_bb && hh.big_blind <= max_bb) result.push_back(hh);
  }
  return result;
}

}  // namespace phase4
}  // namespace poker_engine
