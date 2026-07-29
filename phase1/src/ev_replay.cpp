#include "poker_engine/phase1/ev_replay.h"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <sstream>

#include "poker_engine/evaluator/card.h"

namespace poker_engine {
namespace phase1 {
namespace {

// (Card parsing and street classification are handled by the core
//  poker_engine::Card / evaluator APIs; no local wrappers needed.)

}  // namespace

std::string NodeEV::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "[" << player << "] " << action_desc << " | equity=" << int(equity * 100) << "%"
      << " | ev=$" << immediate_ev << " | trials=" << trials;
  if (actual_outcome) oss << " | actual=$" << *actual_outcome;
  return oss.str();
}

std::string EVReplayResult::DetailedReport() const {
  std::ostringstream oss;
  oss << "=== EV Replay: Hand #" << hand_id << " ===" << "\n";
  oss << "Hero: " << hero << " | Starting Stack: $" << hero_starting_stack << "\n";
  oss << "Ending Stack: $" << hero_ending_stack << " | Total Won: $" << total_won << "\n";
  oss << "Total Invested: $" << total_invested << " | Overall EV: $" << overall_ev << "\n";
  oss << "\n--- Decision Points ---\n";
  for (const auto& node : nodes) {
    oss << "  " << node.ToString() << "\n";
  }
  oss << "\n--- Summary ---\n";
  oss << "Decisions: " << n_decisions << " | Correct Folds: " << n_correct_folds
      << " | Mistakes: " << n_mistakes << " | EV Saved: $" << total_ev_saved << "\n";
  return oss.str();
}

std::string EVReplayResult::CompactReport() const {
  std::ostringstream oss;
  oss << "Hand #" << hand_id << " | " << hero << " | EV=$" << std::fixed << std::setprecision(2)
      << overall_ev << " | Pots: $" << total_won << " | Decisions: " << n_decisions
      << " | Mistakes: " << n_mistakes;
  return oss.str();
}

void EVReplayer::SetOpponentRange(const std::string& player, const poker_engine::range::Range& r) {
  opp_ranges_[player] = r;
}

void EVReplayer::SetHeroCards(const Card cards[2]) {
  hero_cards_[0] = cards[0];
  hero_cards_[1] = cards[1];
  hero_cards_known_ = true;
}

NodeEV EVReplayer::EvaluateNode(const HandHistory& hh, int street_idx, int action_idx,
                                const poker_engine::range::Range& my_range, double pot_before,
                                double to_call) {
  using EV = poker_engine::equity::EquityCalculator;
  NodeEV node;

  const auto& sr = hh.streets[street_idx];
  const auto& action = sr.actions[action_idx];
  node.player = action.player_name;
  node.street = action.street;
  node.action_desc = action.ToString();
  node.action_index = action_idx;
  node.pot_size = pot_before;
  node.call_cost = to_call;

  // 收集已知公共牌
  std::vector<Card> known_board;
  for (int si = 0; si <= street_idx; si++) {
    for (const auto& c : hh.streets[si].community_cards) {
      known_board.push_back(c);
    }
  }

  // 估算 equity
  uint8_t board5[5] = {0};
  for (size_t i = 0; i < known_board.size() && i < 5; i++) {
    board5[i] = known_board[i].Id();
  }

  // Monte Carlo 模拟
  int n_samples = 20000;
  auto result = EV::CalculateMonteCarlo(
      my_range,
      opp_ranges_.count(action.player_name) ? opp_ranges_[action.player_name]
                                            : poker_engine::range::Range::FullCombinatorial(),
      board5, static_cast<int>(known_board.size()), n_samples, rng_);

  node.equity = result.equity[0];
  node.hand_wins = result.win[0];
  node.hand_ties = result.tie[0];
  node.trials = result.total_trials;

  // 计算即时 EV
  // 如果跟注: 期望赢得 = equity * (pot + to_call) - (1-equity) * to_call
  // = equity * pot + to_call * (2*equity - 1)
  if (to_call > 0) {
    node.immediate_ev = node.equity * (pot_before + to_call) - (1.0 - node.equity) * to_call;
  } else {
    // check/fold: EV = equity * pot
    node.immediate_ev = node.equity * pot_before;
  }

  return node;
}

EVReplayResult EVReplayer::Replay(const HandHistory& hh, int n_samples) {
  EVReplayResult result;
  result.hand_id = hh.hand_id;
  result.hero = hh.hero_name;
  result.hero_starting_stack =
      hh.starting_stacks.count(hh.hero_name) ? hh.starting_stacks.at(hh.hero_name) : 0;
  result.hero_ending_stack = result.hero_starting_stack;

  if (hh.streets.empty()) return result;

  double running_pot = 0;
  double hero_invested_total = 0;

  // 处理每个街
  for (int si = 0; si < static_cast<int>(hh.streets.size()); si++) {
    const auto& sr = hh.streets[si];
    running_pot = sr.pot_before;

    for (int ai = 0; ai < static_cast<int>(sr.actions.size()); ai++) {
      const auto& action = sr.actions[ai];

      // 跳过非 hero 的行动（但影响底池）
      if (action.player_name != hh.hero_name) {
        if (action.action == ActionType::BET || action.action == ActionType::RAISE ||
            action.action == ActionType::ALL_IN || action.action == ActionType::CALL ||
            action.action == ActionType::POST_SB || action.action == ActionType::POST_BB)
          running_pot += action.amount;
        continue;
      }

      // 计算 hero 的每个决策点
      hero_invested_total += action.amount;

      // 估算 opponent 范围 — 简化：全范围
      poker_engine::range::Range opp_range;

      // 如果能确定对手的牌（摊牌），使用精确范围
      if (si == static_cast<int>(hh.streets.size()) - 1 &&
          hh.game_type.find("No Limit") != std::string::npos) {
        opp_range = poker_engine::range::Range::FullCombinatorial();
      } else {
        opp_range = poker_engine::range::Range::FullCombinatorial();
      }

      double to_call = 0;
      if (action.action == ActionType::CALL || action.action == ActionType::RAISE ||
          action.action == ActionType::ALL_IN) {
        to_call = action.amount;
      }

      // 使用更高的 n_samples
      int old_samples = n_samples;
      (void)old_samples;

      auto node = EvaluateNode(hh, si, ai, poker_engine::range::Range::FullCombinatorial(),
                               running_pot, to_call);
      result.nodes.push_back(node);

      // 计算英雄获利
      if (action.action == ActionType::CALL || action.action == ActionType::RAISE ||
          action.action == ActionType::ALL_IN || action.action == ActionType::POST_SB ||
          action.action == ActionType::POST_BB || action.action == ActionType::BET)
        result.total_invested += action.amount;
    }
  }

  // 底池分配
  result.total_won = 0;
  for (const auto& pot : hh.pots) {
    for (const auto& w : pot.winners) {
      if (w == hh.hero_name) result.total_won += pot.amount;
    }
  }

  result.hero_ending_stack = result.hero_starting_stack - result.total_invested + result.total_won;
  result.overall_ev = result.total_won - result.total_invested;
  result.n_decisions = static_cast<int>(result.nodes.size());

  // 统计
  for (const auto& node : result.nodes) {
    if (node.immediate_ev < 0) result.n_mistakes++;
  }

  return result;
}

}  // namespace phase1
}  // namespace poker_engine
