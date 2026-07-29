#include <cstdlib>
#include <iostream>

#include "poker_engine/game/table.h"

using namespace poker_engine::game;

int main(int argc, char* argv[]) {
  int num_players = (argc >= 2) ? std::atoi(argv[1]) : 6;
  int hands = (argc >= 3) ? std::atoi(argv[2]) : 10;

  std::cout << "=== Auto Poker Table ===\n";
  std::cout << "Players: " << num_players << " | Hands: " << hands << "\n";

  TableConfig cfg;
  cfg.max_players = num_players;
  cfg.small_blind = 1;
  cfg.big_blind = 2;
  cfg.min_buy_in = 20;
  cfg.max_buy_in = 400;

  Table table(cfg);
  int hand_count = 0;

  table.SetCallback([&](const TableEvent& event) {
    if (event.type == TableEvent::Type::HAND_STARTED) {
      hand_count++;
      if (hand_count % 10 == 0) std::cout << "--- Hand " << hand_count << " ---\n";
    }
  });

  // Add players
  for (int i = 0; i < num_players; i++) {
    int pid = 1000 + i;
    table.JoinTable(pid, "Bot" + std::to_string(i + 1), 200);
    table.SitDown(pid);
  }

  std::cout << "Players seated. Running " << hands << " hands...\n";

  while (hand_count < hands) {
    if (!table.GetGameState().IsHandInProgress()) {
      table.StartHand();
    }
  }

  std::cout << "\nSimulation complete: " << hand_count << " hands played.\n";
  std::cout << table.ToString() << "\n";
  return 0;
}
