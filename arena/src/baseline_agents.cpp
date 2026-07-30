#include "poker_engine/arena/baseline_agents.h"

#include "poker_engine/game/action.h"
#include "poker_engine/game/observation.h"
#include "poker_engine/game/player_state.h"

namespace poker_engine::arena {

using poker_engine::game::ActionType;
using poker_engine::game::Chips;
using poker_engine::game::GameAction;
using poker_engine::game::Observation;
using poker_engine::game::PlayerView;
using poker_engine::network::DecisionRequest;
using poker_engine::network::DecisionResponse;

namespace {

GameAction FoldFor(int32_t player_id) {
  GameAction fold;
  fold.type = ActionType::FOLD;
  fold.player_id = player_id;
  return fold;
}

// Return the first legal action of `type`, or nullptr if absent.
const GameAction* Find(const std::vector<GameAction>& legal, ActionType type) {
  for (const auto& a : legal)
    if (a.type == type) return &a;
  return nullptr;
}

}  // namespace

DecisionResponse CallStationAgent::Decide(const DecisionRequest& request) {
  DecisionResponse response;
  response.confidence = 0.0f;
  response.reason = "call-station baseline (never bets/raises)";

  const std::vector<GameAction>& legal = request.legal_actions;
  const GameAction* chosen = Find(legal, ActionType::CHECK);
  if (!chosen) chosen = Find(legal, ActionType::CALL);
  if (!chosen) chosen = Find(legal, ActionType::FOLD);

  GameAction action = chosen ? *chosen : FoldFor(request.player_id);
  action.player_id = request.player_id;
  response.action = action;
  return response;
}

DecisionResponse ManiacAgent::Decide(const DecisionRequest& request) {
  DecisionResponse response;
  response.confidence = 0.0f;
  response.reason = "maniac baseline (maximum aggression)";

  const std::vector<GameAction>& legal = request.legal_actions;
  const GameAction* chosen = Find(legal, ActionType::RAISE);
  if (!chosen) chosen = Find(legal, ActionType::BET);
  const bool aggressive = chosen != nullptr;
  if (!chosen) chosen = Find(legal, ActionType::CALL);
  if (!chosen) chosen = Find(legal, ActionType::CHECK);
  if (!chosen) chosen = Find(legal, ActionType::FOLD);

  GameAction action = chosen ? *chosen : FoldFor(request.player_id);

  // Shove: size the bet/raise to all-in (min-legal amount up to our whole stack).
  if (aggressive) {
    const Observation& obs = request.observation;
    const PlayerView* me = obs.Me();
    const Chips my_chips = me ? me->chips : 0;
    const Chips my_current = me ? me->bet_info.current_bet : 0;
    const Chips max_total = my_chips + my_current;
    if (max_total > action.amount) action.amount = max_total;
  }

  action.player_id = request.player_id;
  response.action = action;
  return response;
}

}  // namespace poker_engine::arena
