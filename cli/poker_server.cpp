#include <iostream>
#include <sstream>
#include <string>

#include "poker_engine/game/table.h"

using namespace poker_engine::game;

void PrintHelp() {
  std::cout << "\n=== Poker Server v1.2 ===\n"
            << "  new <name> <max> <sb> <bb>  Create table\n"
            << "  join <name> <buyin>         Join table\n"
            << "  sit <player_id>             Sit down\n"
            << "  start                       Start hand\n"
            << "  act <pid> <fold|check|call|bet|raise|allin> [amount]\n"
            << "  state                       Show state\n"
            << "  help                        This help\n"
            << "  quit                        Exit\n\n";
}

int main() {
  Table* table = nullptr;
  int next_pid = 1;

  std::cout << "=== Poker Engine v1.2 — Game Server ===\n";
  PrintHelp();

  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    if (line.empty()) continue;
    if (line == "quit" || line == "exit") break;
    if (line == "help") {
      PrintHelp();
      continue;
    }

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "new") {
      std::string name = "Default";
      int max = 6;
      double sb = 0.5, bb = 1.0;
      iss >> name >> max >> sb >> bb;
      delete table;
      TableSettings cfg;
      cfg.name = name;
      cfg.max_players = max;
      cfg.small_blind = sb;
      cfg.big_blind = bb;
      cfg.min_buy_in = bb * 10;
      cfg.max_buy_in = bb * 200;
      table = new Table(1, cfg);
      table->SetCallback([](const TableEvent& e) { std::cout << "[EVENT] " << e.message << "\n"; });
      std::cout << "Table '" << name << "' created.\n";
    } else if (cmd == "join") {
      if (!table) {
        std::cout << "Create a table first.\n";
        continue;
      }
      std::string name;
      double buyin;
      iss >> name >> buyin;
      int pid = next_pid++;
      if (table->JoinTable(pid, name, buyin)) {
        std::cout << pid << " joined as " << name << "\n";
      } else {
        std::cout << "Join failed.\n";
      }
    } else if (cmd == "sit") {
      if (!table) continue;
      int pid;
      iss >> pid;
      table->SitDown(pid);
      std::cout << "Player " << pid << " sat down\n";
    } else if (cmd == "start") {
      if (!table) continue;
      table->StartHand();
    } else if (cmd == "act") {
      if (!table) continue;
      int pid;
      std::string action_str;
      double amount = 0;
      iss >> pid >> action_str >> amount;

      GameAction ga;
      if (action_str == "fold")
        ga.type = ActionType::FOLD;
      else if (action_str == "check")
        ga.type = ActionType::CHECK;
      else if (action_str == "call") {
        ga.type = ActionType::CALL;
        ga.amount = amount;
      } else if (action_str == "bet") {
        ga.type = ActionType::BET;
        ga.amount = amount;
      } else if (action_str == "raise") {
        ga.type = ActionType::RAISE;
        ga.amount = amount;
      } else if (action_str == "allin")
        ga.type = ActionType::ALL_IN;
      else {
        std::cout << "Unknown action\n";
        continue;
      }

      table->PlayerAction(pid, ga);
    } else if (cmd == "state") {
      if (!table) {
        std::cout << "No table\n";
        continue;
      }
      std::cout << table->ToString() << "\n";
    } else {
      std::cout << "Unknown command. Type 'help'.\n";
    }
  }

  delete table;
  return 0;
}
