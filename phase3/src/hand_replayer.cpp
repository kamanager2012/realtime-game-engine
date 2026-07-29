#include "poker_engine/phase3/hand_replayer.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase3 {

using namespace poker_engine;
using namespace poker_engine::phase1;
using namespace poker_engine::range;
using namespace poker_engine::equity;

std::string ReplayNode::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "[" << street_name << "] " << player << " " << action_desc;
  oss << " (pot: $" << pot_before;
  if (amount > 0) oss << " + $" << amount;
  oss << ")";
  if (hero_equity > 0 || villain_equity > 0) {
    oss << " | Eq: Hero " << int(hero_equity * 100) << "%";
    if (villain_equity > 0) oss << " Villain " << int(villain_equity * 100) << "%";
  }
  if (!recommendation.empty()) oss << " [" << recommendation << "]";
  return oss.str();
}

std::string ReplayResult::Summary() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "Hand #" << hand_id << " | " << hero_name << " [" << hero_cards_str << "]\n";
  oss << "Result: " << result_type << " | Net: $" << net_profit << "\n";
  oss << "Plays: " << good_plays << " good, " << mistakes << " mistakes\n";
  oss << "Total EV: $" << total_hero_ev << " | Actual: $" << total_actual;
  return oss.str();
}

std::string ReplayResult::FullReport() const {
  std::ostringstream oss;
  oss << Summary() << "\n\n";
  oss << "--- Action-by-Action ---\n";
  for (const auto& n : nodes) {
    oss << "  " << n.ToString() << "\n";
  }
  return oss.str();
}

HandReplayer::HandReplayer() {}

void HandReplayer::SetHeroCards(const std::string& card1, const std::string& card2) {
  hero_card1_ = card1;
  hero_card2_ = card2;
}

void HandReplayer::SetVillainRange(const std::string& range_str) { villain_range_str_ = range_str; }

ReplayResult HandReplayer::Replay(const HandHistory& hh) {
  ReplayResult result;
  result.hand_id = hh.hand_id;
  result.hero_name = hh.hero_name;
  result.hero_cards_str =
      Card(hh.hero_cards[0]).ToString() + " " + Card(hh.hero_cards[1]).ToString();

  if (hh.streets.empty()) return result;

  // 确定 Hero 范围 — 从已知底牌构建精确范围
  Range hero_range;
  uint8_t c1_id = hh.hero_cards[0].Id();
  uint8_t c2_id = hh.hero_cards[1].Id();
  hero_range.Set(HandId::Encode(std::min(c1_id, c2_id), std::max(c1_id, c2_id)), 1.0f);

  Range villain_range = villain_range_str_.empty() ? Range::FullCombinatorial()
                                                   : Range::FromString(villain_range_str_);

  double running_pot = hh.total_pot;
  result.total_actual = hh.HeroNetProfit();

  // 收集所有公共牌
  std::vector<Card> all_board_cards;
  for (const auto& sr : hh.streets) {
    for (const auto& c : sr.community_cards) {
      all_board_cards.push_back(c);
    }
  }

  // 对每个决策点进行分析
  for (size_t si = 0; si < hh.streets.size(); si++) {
    const auto& sr = hh.streets[si];
    const char* street_names[] = {"Preflop", "Flop", "Turn", "River"};
    const char* sn = (si < 4) ? street_names[si] : "Unknown";

    for (size_t ai = 0; ai < sr.actions.size(); ai++) {
      const auto& action = sr.actions[ai];
      if (action.player_name != hh.hero_name) continue;

      ReplayNode node;
      node.street_name = sn;
      node.player = action.player_name;
      node.pot_before = sr.pot_before;
      node.amount = action.amount;

      switch (action.action) {
        case ActionType::FOLD:
          node.action_desc = "folds";
          break;
        case ActionType::CHECK:
          node.action_desc = "checks";
          break;
        case ActionType::CALL:
          node.action_desc = "calls $" + std::to_string((int)action.amount);
          break;
        case ActionType::BET:
          node.action_desc = "bets $" + std::to_string((int)action.amount);
          break;
        case ActionType::RAISE:
          node.action_desc = "raises to $" + std::to_string((int)action.amount);
          break;
        case ActionType::ALL_IN:
          node.action_desc = "all-in $" + std::to_string((int)action.amount);
          break;
        default:
          node.action_desc = ActionTypeToString(action.action);
          break;
      }

      // 计算当前已知公共牌的 equity
      int board_size = 0;
      if (si >= 1 && all_board_cards.size() >= 3) board_size = 3;
      if (si >= 2 && all_board_cards.size() >= 4) board_size = 4;
      if (si >= 3 && all_board_cards.size() >= 5) board_size = 5;

      if (board_size > 0) {
        uint8_t board_copy[5] = {0};
        for (int i = 0; i < board_size && i < (int)all_board_cards.size(); i++) {
          board_copy[i] = all_board_cards[i].Id();
        }

        auto eq_result = EquityCalculator::CalculateMonteCarlo(hero_range, villain_range,
                                                               board_copy, board_size, 20000, rng_);

        node.hero_equity = eq_result.equity[0];
        node.villain_equity = eq_result.equity[1];

        // 计算每个可能动作的 EV
        double best_ev = -1e10;
        std::string best_action_name = "Fold";

        // Fold EV
        double fold_ev = -node.pot_before * 0.1;
        if (fold_ev > best_ev) {
          best_ev = fold_ev;
          best_action_name = "Fold";
        }

        // Check/Call EV
        double call_ev = node.hero_equity * running_pot - (1.0 - node.hero_equity) * action.amount;
        if (call_ev > best_ev) {
          best_ev = call_ev;
          best_action_name = "Call";
        }

        // Bet EV
        double bet_size = action.amount;
        double bet_ev =
            node.hero_equity * (running_pot + bet_size) - (1.0 - node.hero_equity) * bet_size;
        if (bet_ev > best_ev) {
          best_ev = bet_ev;
          best_action_name = "Bet";
        }

        node.hero_ev = best_ev;
        node.ev_diff = call_ev - best_ev;

        // 判断是否是合理动作
        double actual_action_ev = -1e10;
        if (action.action == ActionType::FOLD)
          actual_action_ev = fold_ev;
        else if (action.action == ActionType::CHECK || action.action == ActionType::CALL)
          actual_action_ev = call_ev;
        else
          actual_action_ev = bet_ev;

        double diff = actual_action_ev - best_ev;
        if (diff > 0.01) {
          node.recommendation = "+";
          result.good_plays++;
        } else if (diff < -0.01) {
          node.recommendation = "-";
          result.mistakes++;
        } else {
          node.recommendation = "=";
        }
      }

      result.nodes.push_back(node);
    }
  }

  result.total_hero_ev = 0;
  for (const auto& n : result.nodes) {
    if (n.hero_ev.has_value() && n.hero_ev.value() != 0) result.total_hero_ev += n.hero_ev.value();
  }

  if (hh.HeroNetProfit() > 0) {
    result.result_type = "Win $" + std::to_string((int)hh.HeroNetProfit());
  } else if (hh.HeroNetProfit() < 0) {
    result.result_type = "Lose $" + std::to_string((int)std::abs(hh.HeroNetProfit()));
  } else {
    result.result_type = "Tie";
  }

  return result;
}

}  // namespace phase3
}  // namespace poker_engine
