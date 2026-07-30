#include "poker_engine/arena/random_agent.h"

#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/game/player_state.h"

namespace poker_engine::arena {

using poker_engine::game::ActionType;
using poker_engine::game::Chips;
using poker_engine::game::GameAction;
using poker_engine::game::GameState;
using poker_engine::game::PlayerState;
using poker_engine::network::AIConfig;
using poker_engine::network::DecisionRequest;
using poker_engine::network::DecisionResponse;

RandomAgent::RandomAgent(const AIConfig& config)
    : config_(config), rng_(static_cast<uint64_t>(config.random_seed)) {}

void RandomAgent::Initialize(const AIConfig& config) {
  config_ = config;
  rng_.seed(static_cast<uint64_t>(config.random_seed));
}

DecisionResponse RandomAgent::Decide(const DecisionRequest& request) {
  DecisionResponse response;
  response.confidence = 0.0f;
  response.reason = "uniform-random baseline";

  // Prefer the caller-supplied legal set; otherwise ask the engine directly.
  std::vector<GameAction> legal = request.legal_actions;
  if (legal.empty()) legal = request.state.LegalActions(request.player_id);

  if (legal.empty()) {
    // Nothing legal (not our turn / already all-in): fold defensively.
    GameAction fold;
    fold.type = ActionType::FOLD;
    fold.player_id = request.player_id;
    response.action = fold;
    return response;
  }

  std::uniform_int_distribution<size_t> pick(0, legal.size() - 1);
  GameAction chosen = legal[pick(rng_)];

  // Size bets/raises uniformly between the minimum and all-in.
  if (chosen.type == ActionType::BET || chosen.type == ActionType::RAISE) {
    Chips my_chips = 0;
    Chips my_current = 0;
    for (const auto& p : request.state.AllPlayers()) {
      if (p.id == request.player_id) {
        my_chips = p.chips;
        my_current = p.bet_info.current_bet;
        break;
      }
    }
    const Chips max_total = my_chips + my_current;
    if (max_total > chosen.amount) {
      std::uniform_int_distribution<int64_t> size(chosen.amount, max_total);
      chosen.amount = size(rng_);
    }
  }

  chosen.player_id = request.player_id;
  response.action = chosen;
  return response;
}

void RandomAgent::OnHandComplete(const GameState&) {}

bool RandomAgent::ReloadModel(const std::string&) { return false; }

}  // namespace poker_engine::arena
