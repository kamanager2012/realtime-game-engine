#include "poker_engine/network/cfr_policy_store.h"

#include <algorithm>
#include <cmath>

namespace poker_engine::network {

namespace {
using poker_engine::cfr::Action;
using poker_engine::game::ActionType;
using poker_engine::game::GameAction;
using poker_engine::game::GameState;
using poker_engine::game::PlayerState;

ActionType MapCfrAction(Action cfr_action, double to_call) {
  switch (cfr_action) {
    case Action::Fold:
      return ActionType::FOLD;
    case Action::Call:
      return to_call <= 0.001 ? ActionType::CHECK : ActionType::CALL;
    case Action::BetHalf:
    case Action::BetPot:
      return to_call <= 0.001 ? ActionType::BET : ActionType::RAISE;
    case Action::AllIn:
      return ActionType::ALL_IN;
    default:
      return ActionType::FOLD;
  }
}

double BetAmountForCfr(Action cfr_action, double pot, double to_call, double min_raise_to,
                       double stack, double current_bet, double my_bet) {
  switch (cfr_action) {
    case Action::BetHalf:
      return std::min(stack, std::max(to_call > 0.001 ? min_raise_to : pot * 0.5, pot * 0.5));
    case Action::BetPot:
      return std::min(stack, std::max(to_call > 0.001 ? min_raise_to : pot, pot));
    case Action::AllIn:
      return 0;
    case Action::Call:
      return 0;
    default:
      return 0;
  }
}

Action SampleFromDistribution(const std::vector<std::pair<Action, double>>& dist, std::mt19937& rng) {
  if (dist.empty()) return Action::Fold;
  double total = 0;
  for (const auto& [_, p] : dist) total += std::max(0.0, p);
  if (total <= 1e-9) return dist.front().first;

  std::uniform_real_distribution<double> uni(0.0, total);
  double roll = uni(rng);
  double acc = 0;
  for (const auto& [a, p] : dist) {
    acc += std::max(0.0, p);
    if (roll <= acc) return a;
  }
  return dist.back().first;
}

}  // namespace

CfrPolicyStore& CfrPolicyStore::Instance() {
  static CfrPolicyStore store;
  return store;
}

bool CfrPolicyStore::LoadFromFile(const std::string& path) {
  std::lock_guard<std::mutex> lock(mu_);
  loaded_ = policy_.LoadFromFile(path);
  model_path_ = loaded_ ? path : std::string();
  return loaded_;
}

bool CfrPolicyStore::IsLoaded() const {
  std::lock_guard<std::mutex> lock(mu_);
  return loaded_;
}

size_t CfrPolicyStore::NodeCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return loaded_ ? policy_.NodeCount() : 0;
}

std::string CfrPolicyStore::ModelPath() const {
  std::lock_guard<std::mutex> lock(mu_);
  return model_path_;
}

std::optional<GameAction> CfrPolicyStore::SampleAction(const GameState& state, int32_t player_id,
                                                       std::mt19937& rng) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!loaded_) return std::nullopt;

  const PlayerState* me = nullptr;
  for (const auto& p : state.AllPlayers()) {
    if (p.id == player_id) {
      me = &p;
      break;
    }
  }
  if (!me || !me->IsActive()) return std::nullopt;

  auto dist = policy_.GetActionDistribution(state, player_id);
  if (dist.empty()) return std::nullopt;

  Action picked = SampleFromDistribution(dist, rng);
  double to_call = state.GetCurrentBet() - me->bet_info.current_bet;
  if (to_call < 0) to_call = 0;
  const double pot = state.GetPot();
  const double bb = 2.0;
  const double min_raise_to = state.GetCurrentBet() + bb;

  GameAction ga;
  ga.player_id = player_id;
  ga.type = MapCfrAction(picked, to_call);
  ga.amount = BetAmountForCfr(picked, pot, to_call, min_raise_to, me->chips, state.GetCurrentBet(),
                              me->bet_info.current_bet);
  return ga;
}

}  // namespace poker_engine::network
