// Minimal demo: an optional real LLM agent vs a RandomAgent baseline, printing
// each decision and its reason. Compiled only when BUILD_EXAMPLES=ON.
//
// Run it with:
//   OPENAI_API_KEY=sk-... ./build/examples/llm_agent_demo
// Without OPENAI_API_KEY the LLM agent gracefully falls back to a safe passive
// baseline (no network calls) so the demo still runs end to end.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "llm_agent.h"

#include "poker_engine/arena/random_agent.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/game_state.h"
#include "poker_engine/network/ai_engine.h"

using poker_engine::arena::RandomAgent;
using poker_engine::examples::LlmAgent;
using poker_engine::game::GameAction;
using poker_engine::game::GameState;
using poker_engine::game::TableConfig;
using poker_engine::network::AIConfig;
using poker_engine::network::DecisionRequest;
using poker_engine::network::DecisionResponse;
using poker_engine::network::IAIEngine;

int main() {
  TableConfig table;
  table.max_players = 6;
  table.small_blind = 50;
  table.big_blind = 100;
  table.ante = 0;
  table.min_buy_in = 1000;
  table.max_buy_in = 1000000;

  AIConfig llm_cfg;
  llm_cfg.name = "LLM";
  LlmAgent llm(llm_cfg);

  AIConfig rnd_cfg;
  rnd_cfg.name = "Random";
  rnd_cfg.random_seed = 22;
  RandomAgent rnd(rnd_cfg);

  std::printf("LLM agent mode: %s\n\n", llm.HasApiKey() ? "live (OPENAI_API_KEY set)"
                                                        : "safe fallback (no OPENAI_API_KEY)");

  GameState state(table);
  state.SetDeterministicDeckSeed(7);
  state.AddPlayerAtSeat(1, "LLM", 20000, 0);
  state.AddPlayerAtSeat(2, "Random", 20000, 1);

  const int kHands = 3;
  for (int h = 1; h <= kHands; ++h) {
    if (!state.StartHand()) break;
    std::printf("=== Hand %d ===\n", h);

    int guard = 0;
    while (state.IsHandInProgress() && guard++ < 5000) {
      const int32_t cur = state.GetCurrentPlayerId();
      if (cur < 0) break;
      std::vector<GameAction> legal = state.LegalActions(cur);
      if (legal.empty()) break;

      IAIEngine& agent = (cur == 1) ? static_cast<IAIEngine&>(llm)
                                    : static_cast<IAIEngine&>(rnd);
      DecisionRequest req{state.ObserveFor(cur), cur, legal};
      DecisionResponse resp = agent.Decide(req);
      GameAction act = resp.action;
      act.player_id = cur;

      std::printf("  P%d (%s): %s  [%s]\n", cur, agent.Name().c_str(), act.ToString().c_str(),
                  resp.reason.c_str());
      if (!state.ProcessAction(cur, act)) {
        std::printf("  (rejected action, stopping hand)\n");
        break;
      }
    }

    std::printf("  stacks:");
    for (const auto& p : state.AllPlayers()) {
      if (p.id == 1 || p.id == 2) std::printf(" P%d=%lld", p.id, static_cast<long long>(p.chips));
    }
    std::printf("\n\n");
  }

  return 0;
}
