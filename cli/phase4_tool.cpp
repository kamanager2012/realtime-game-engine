#include <iomanip>
#include <iostream>
#include <string>

#include "poker_engine/phase1/ev_replay.h"
#include "poker_engine/phase1/hand_history.h"
#include "poker_engine/phase4/hh_parser.h"
#include "poker_engine/phase4/multi_street_solver.h"
#include "poker_engine/phase4/session_analyzer.h"
#include "poker_engine/range/range.h"

using namespace poker_engine;
using namespace poker_engine::phase4;
using namespace poker_engine::range;

void PrintUsage(const char* prog) {
  std::cout << "Poker Phase 4 Tool v0.4\n"
            << "Usage: " << prog << " <command> [args...]\n\n"
            << "Commands:\n"
            << "  parse <file>              Parse a hand history file\n"
            << "  parse-dir <dir>           Parse all .txt files in directory\n"
            << "  solve-flop <hero> <villain> <flop_cards>\n"
            << "                            Multi-street solve from flop\n"
            << "  session <dir>             Compute session stats from directory\n"
            << "  history-to-phase1 <file>  Convert HH to Phase1 HandHistory\n"
            << "\nExamples:\n"
            << "  " << prog << " parse hand.txt\n"
            << "  " << prog << " solve-flop 'AKs' '22+' 'QhJd7c'\n"
            << "  " << prog << " session ./hands/\n";
}

// ============ PARSE ============
void CmdParse(const std::string& filepath) {
  HandHistoryParser parser;
  auto result = parser.ParseFromFile(filepath);

  if (!result.parsed_ok) {
    std::cerr << "Parse failed: " << result.error_msg << "\n";
    return;
  }

  std::cout << result.hh.ToString() << "\n";
  std::cout << "\n--- Short Summary ---\n";
  std::cout << result.hh.ToShortSummary() << "\n";
}

// ============ PARSE DIRECTORY ============
void CmdParseDir(const std::string& dir_path) {
  HandHistoryParser parser;
  auto hands = parser.ParseFromDirectory(dir_path);

  std::cout << "Parsed " << hands.size() << " hands from " << dir_path << "\n";
  std::cout << "Success rate: " << parser.TotalHandsParsed() << " parsed, " << parser.FailedParses()
            << " failed\n\n";

  for (size_t i = 0; i < std::min(hands.size(), size_t(5)); i++) {
    std::cout << "  " << hands[i].ToShortSummary() << "\n";
  }
  if (hands.size() > 5) {
    std::cout << "  ... and " << (hands.size() - 5) << " more\n";
  }
}

// ============ MULTI-STREET SOLVE ============
void CmdSolveFlop(const std::string& hero_str, const std::string& villain_str,
                  const std::string& flop_str) {
  std::cout << "\n=== Multi-Street CFRA Solve ===\n\n";

  MS_Config config;
  config.iterations = 200;
  config.mc_samples = 1000;
  config.verbose = true;

  MultiStreetSolver solver(config);

  solver.SetRanges({Range::FromString(hero_str), Range::FromString(villain_str)});

  // Parse flop cards
  std::vector<Card> flop;
  for (size_t i = 0; i + 1 < flop_str.size(); i += 2) {
    flop.push_back(Card::Parse(flop_str.substr(i, 2)));
  }

  if (flop.size() >= 3) {
    solver.SetFlop(flop);
    auto result = solver.SolveFromHand(HandHistory());
    std::cout << "\n" << result.ToString() << "\n";

    // Print strategy visualization
    std::cout << "\n=== Strategy Visualization ===\n";
    std::cout << StrategyVisualization::PrintAllNodes(result.strategies);
  } else {
    std::cerr << "Need at least 3 flop cards\n";
  }
}

// ============ SESSION ANALYSIS ============
void CmdSession(const std::string& dir_path) {
  SessionAnalyzer analyzer;
  analyzer.LoadDirectory(dir_path);

  auto stats = analyzer.ComputeStats();
  std::cout << stats.ToString() << "\n";
}

// ============ HH → Phase1 Conversion ============
void CmdHistoryToPhase1(const std::string& filepath) {
  HandHistoryParser parser;
  auto result = parser.ParseFromFile(filepath);

  if (!result.parsed_ok) {
    std::cerr << "Parse failed: " << result.error_msg << "\n";
    return;
  }

  // Convert to Phase1 HandHistory format and show EV replay
  poker_engine::phase1::HandHistory hh1;
  hh1.hand_id = result.hh.hand_id;
  hh1.site = result.hh.site;
  hh1.game_type = result.hh.game_type;
  hh1.table_name = result.hh.table_name;
  hh1.small_blind = result.hh.small_blind;
  hh1.big_blind = result.hh.big_blind;
  hh1.max_seats = result.hh.max_seats;
  hh1.hero_name = result.hh.HeroName();

  for (const auto& [name, seat] : result.hh.seat_map) {
    hh1.starting_stacks[name] = seat.stack;
  }

  if (auto hc = result.hh.HeroCards(); hc.size() >= 2) {
    hh1.hero_cards[0] = hc[0];
    hh1.hero_cards[1] = hc[1];
    hh1.hero_cards_known_ = true;
  }

  for (const auto& st : result.hh.streets) {
    poker_engine::phase1::StreetRound sr;
    sr.street = static_cast<poker_engine::phase1::Street>(static_cast<int>(st.street));
    sr.community_cards = st.community_cards;
    sr.pot_before = 15;  // simplified

    for (const auto& a : st.actions) {
      poker_engine::phase1::PlayerAction pa;
      pa.player_name = a.player_name;
      pa.action = static_cast<poker_engine::phase1::ActionType>(static_cast<int>(a.action));
      pa.amount = a.amount;
      pa.street = static_cast<poker_engine::phase1::Street>(static_cast<int>(st.street));
      pa.raw_line = a.raw_line;
      sr.actions.push_back(pa);
    }
    hh1.streets.push_back(sr);
  }

  hh1.board = result.hh.all_board_cards;
  hh1.total_pot = result.hh.total_pot;

  std::cout << "\n--- Converted Hand Summary ---\n";
  std::cout << hh1.Summary() << "\n";

  // Run EV replay
  poker_engine::phase1::EVReplayer replayer;
  if (hh1.hero_cards_known_) {
    replayer.SetHeroCards(hh1.hero_cards);
  }

  auto ev_result = replayer.Replay(hh1, 10000);
  std::cout << ev_result.CompactReport() << "\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];

  if (cmd == "parse" && argc >= 3) {
    CmdParse(argv[2]);
  } else if (cmd == "parse-dir" && argc >= 3) {
    CmdParseDir(argv[2]);
  } else if (cmd == "solve-flop" && argc >= 5) {
    CmdSolveFlop(argv[2], argv[3], argv[4]);
  } else if (cmd == "session" && argc >= 3) {
    CmdSession(argv[2]);
  } else if (cmd == "history-to-phase1" && argc >= 3) {
    CmdHistoryToPhase1(argv[2]);
  } else {
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
