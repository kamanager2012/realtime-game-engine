#include "poker_engine/phase11/solver_manager.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "poker_engine/base/logging.h"
#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/evaluator/card.h"
#include "poker_engine/phase10/parallel_preflop_lut.h"
#include "poker_engine/phase11/engine_registry.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase11 {
using namespace poker_engine;
using namespace poker_engine::range;
using namespace poker_engine::equity;

// ===================== AnalysisOutput =====================
std::string AnalysisOutput::ToString() const {
  std::ostringstream oss;
  oss << "=== " << title << " ==="
      << " [time: " << compute_time_seconds << "s]\n\n"
      << content << "\n";
  return oss.str();
}

// ===================== SolverManager =====================
SolverManager& SolverManager::Instance() {
  static SolverManager instance;
  return instance;
}

void SolverManager::Initialize() {
  decision_engine_.SetLevel(DecisionLevel::STANDARD);

  if (!fast_solver_.LoadLUT("/tmp/fast_preflop_lut.bin")) {
    fast_solver_.BuildLUT("/tmp/fast_preflop_lut.bin");
  }

  auto config7 = poker_engine::phase7::CFRConfig{
      500, 500, poker_engine::phase7::CFRMode::DISCOUNTED, 1.5, 0.5, 2.0, 0.0, false};
  cfr_plus_ = std::make_unique<poker_engine::phase7::CFRPlusSolver>(config7);

  auto config6 = poker_engine::phase6::ICFRConfig{500, 1000, 1.0, 0.5, false};
  icfr_ = std::make_unique<poker_engine::phase6::ICFRSolver>(config6);

  eq_matrix_calc_ = std::make_unique<poker_engine::phase5::EquityMatrixCalculator>();
  eq_matrix_calc_->SetSamples(3000);

  fast_solver_.LoadLUT("/tmp/fast_preflop_lut.bin");
}

// ---- Tokenizer ----
std::vector<std::string> SolverManager::Tokenize(const std::string& text) {
  std::vector<std::string> tokens;
  std::string current;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

bool SolverManager::IsHandDescription(const std::string& token) {
  if (token.size() < 2 || token.size() > 5) return false;

  const char* ranks = "23456789TJQKA";
  bool r1 = (strchr(ranks, token[0]) != nullptr);
  bool r2 = (strchr(ranks, token[1]) != nullptr);
  if (!r1 || !r2) return false;

  if (token.size() >= 3) {
    // Allow s/o, +, and combined range suffixes like "AKs,QQ+"
    char c = token[2];
    if (c == 's' || c == 'o' || c == 'S' || c == 'O' || c == '+') return true;
    // If more chars, might be a range expression — allow it
    return true;
  }
  return true;
}

bool SolverManager::IsPosition(const std::string& token) {
  static const std::vector<std::string> positions = {"UTG", "UTG1", "UTG2", "MP", "MP1",
                                                     "HJ",  "CO",   "BTN",  "SB", "BB"};
  for (const auto& p : positions)
    if (token == p) return true;
  return false;
}

bool SolverManager::IsNumeric(const std::string& token) {
  try {
    std::stod(token);
    return true;
  } catch (const std::exception& e) {
    PE_LOG_WARN("SolverManager::IsNumeric: invalid numeric token '{}': {}", token, e.what());
    return false;
  } catch (...) {
    PE_LOG_WARN("SolverManager::IsNumeric: unknown error parsing token '{}'", token);
    return false;
  }
}

// ---- Analyze ----
AnalysisOutput SolverManager::Analyze(const std::string& query) {
  if (query.length() < 60) {
    return AnalyzeShortQuery(query);
  }
  return AnalyzeLongQuery(query);
}

AnalysisOutput SolverManager::AnalyzeShortQuery(const std::string& query) {
  AnalysisOutput result;
  auto tokens = Tokenize(query);

  if (tokens.empty()) {
    result.title = "Error";
    result.content = "Empty query. Try: 'AKs vs 22+ on QhJd7c' or 'preflop BTN'";
    return result;
  }

  // Pattern 1: "hand1 vs hand2 [on board]"
  for (size_t i = 0; i + 2 <= tokens.size(); i++) {
    if (tokens[i] == "vs" && i > 0 && IsHandDescription(tokens[i - 1]) &&
        IsHandDescription(tokens[i + 1])) {
      std::string hero_str = tokens[i - 1];
      std::string villain_str = tokens[i + 1];
      std::vector<std::string> board;

      if (i + 4 < tokens.size() && tokens[i + 2] == "on") {
        for (size_t j = i + 3; j < tokens.size(); j++) {
          if (tokens[j].size() == 2) board.push_back(tokens[j]);
        }
      }

      return AnalyzeEquity(hero_str, villain_str, board, result);
    }
  }

  // Pattern 2: "preflop [position]"
  if (tokens[0] == "preflop") {
    std::string position = tokens.size() > 1 ? tokens[1] : "BTN";
    return AnalyzePreflop(position, result);
  }

  // Pattern 3: "icm payouts stacks"
  if (tokens[0] == "icm") {
    for (size_t i = 1; i < tokens.size(); i++) {
      if (tokens[i].find(',') != std::string::npos) {
        return AnalyzeICM(tokens[i], i + 1 < tokens.size() ? tokens[i + 1] : "");
      }
    }
  }

  // Pattern 4: single hand inquiry
  if (IsHandDescription(tokens[0])) {
    return AnalyzeSingleHand(
        tokens[0], tokens.size() > 1 && IsPosition(tokens[1]) ? tokens[1] : "BTN", result);
  }

  // Default: try equity query
  if (tokens.size() >= 3) {
    return AnalyzeEquity(tokens[0], tokens[2], {}, result);
  }

  result.title = "Unrecognized Query";
  result.content =
      "Expected patterns:\n"
      "  'AKs vs 22+ on QhJd7c' - equity query\n"
      "  'preflop BTN'         - preflop strategy\n"
      "  'AKs BTN'             - hand analysis\n"
      "  'icm 50,30,20'        - ICM calculation\n";
  return result;
}

AnalysisOutput SolverManager::AnalyzeLongQuery(const std::string& query) {
  AnalysisOutput result;
  result.title = "Detailed Analysis";

  auto tokens = Tokenize(query);

  for (size_t i = 0; i < tokens.size(); i++) {
    if (tokens[i] == "vs" && i > 0 && i + 1 < tokens.size()) {
      std::vector<std::string> board;
      for (size_t j = i + 2; j < tokens.size(); j++) {
        if (tokens[j].size() == 2) board.push_back(tokens[j]);
      }
      return AnalyzeEquity(tokens[i - 1], tokens[i + 1], board, result);
    }
  }

  GameContext ctx;
  ctx.hero_cards = tokens.empty() ? "" : tokens[0];
  for (size_t i = 1; i < tokens.size(); i++) {
    if (tokens[i].size() == 2) ctx.community_cards.push_back(tokens[i]);
  }

  if (!ctx.hero_cards.empty()) {
    auto decision = decision_engine_.Decide(ctx);
    result.title = "Decision: " + ctx.hero_cards;
    result.content = decision.ToString();
  }

  return result;
}

// ---- Analysis Helpers ----
AnalysisOutput SolverManager::AnalyzeEquity(const std::string& hero_str,
                                            const std::string& villain_str,
                                            const std::vector<std::string>& board_strs,
                                            AnalysisOutput& result) {
  auto hero = Range::FromString(hero_str);
  auto villain = Range::FromString(villain_str);

  if (board_strs.empty()) {
    PreflopLUTCalculator calc;
    auto lut = calc.Calculate(1000);

    double avg_equity = 0;
    int count = 0;

    for (int i = 0; i < PreflopLUT::NUM_TYPES; i++) {
      float w_h = hero.Get(i);
      if (w_h <= 0) continue;
      for (int j = 0; j < PreflopLUT::NUM_TYPES; j++) {
        float w_v = villain.Get(j);
        if (w_v <= 0) continue;
        avg_equity += lut.GetEquity(i, j) * w_h * w_v;
        count++;
      }
    }

    if (count > 0) avg_equity /= count;

    result.title = "Pre-flop Equity (LUT)";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << hero_str << " vs " << villain_str << "\n";
    oss << hero_str << " equity: " << avg_equity * 100 << "%\n";
    oss << villain_str << " equity: " << (1.0 - avg_equity) * 100 << "%\n";
    oss << "(Computed via pre-computed LUT, no sampling)\n";
    result.content = oss.str();
  } else {
    uint8_t board5[5] = {0};
    int bs = 0;
    std::string board_str;
    for (const auto& c : board_strs) {
      Card card = Card::Parse(c);
      board5[bs] = card.Id();
      board_str += c + " ";
      bs++;
    }

    std::mt19937 rng(42);
    auto res = EquityCalculator::CalculateMonteCarlo(hero, villain, board5, bs, 10000, rng);

    result.title = "Post-flop Equity on " + board_str;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << hero_str << " vs " << villain_str << " on " << board_str << "\n\n";
    oss << hero_str << " equity: " << static_cast<double>(res.equity[0]) * 100 << "%\n";
    oss << villain_str << " equity: " << static_cast<double>(res.equity[1]) * 100 << "%\n";
    oss << "Tie: " << static_cast<double>(res.tie[0]) * 100 << "%\n";
    oss << "Trials: " << res.total_trials << "\n";
    result.content = oss.str();
  }

  return result;
}

AnalysisOutput SolverManager::AnalyzePreflop(const std::string& position, AnalysisOutput& result) {
  PositionVars default_opp{0.25, 0.15, 0.05};

  auto fast_result = fast_solver_.SolvePosition(position, default_opp);

  result.title = "Preflop Strategy: " + position;
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "Top 20 hands for " << position << " (vs medium opponent):\n\n";

  auto top = fast_result.TopN(20);
  oss << std::setw(6) << "Hand" << std::setw(8) << "Eq%" << std::setw(8) << "EV" << std::setw(8)
      << "Raise%" << std::setw(12) << "Action\n";
  oss << std::string(44, '-') << "\n";

  for (const auto& a : top) {
    oss << std::setw(6) << a.hand_name << std::setw(7) << int(a.equity_vs_1bb_range * 100) << "%"
        << std::setw(8) << int(a.ev_vs_1bb * 100) << "bb" << std::setw(7)
        << int(a.optimal_raise_pct * 100) << "%"
        << "  " << a.category << "\n";
  }

  oss << "\nTime: " << fast_result.solve_time_ms << "ms\n";
  result.content = oss.str();

  return result;
}

AnalysisOutput SolverManager::AnalyzeSingleHand(const std::string& hand,
                                                const std::string& position,
                                                AnalysisOutput& result) {
  PositionVars default_opp{0.25, 0.15, 0.05};
  auto advice = fast_solver_.AnalyzeSingleHand(hand, position, default_opp);

  result.title = hand + " at " + position;
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "Hand: " << hand << "\n";
  oss << "Position: " << position << "\n\n";
  oss << "Equity vs 1BB range: " << advice.equity_vs_1bb_range * 100 << "%\n";
  oss << "EV vs 1BB: " << int(advice.ev_vs_1bb * 100) << " BB\n";
  oss << "Optimal raise: " << int(advice.optimal_raise_pct * 100) << "%\n";
  oss << "Optimal 3bet: " << int(advice.optimal_3bet_pct * 100) << "%\n";
  oss << "Category: " << advice.category << "\n";
  oss << "Score: " << int(advice.score) << "/100\n";

  result.content = oss.str();
  return result;
}

AnalysisOutput SolverManager::AnalyzeICM(const std::string& payouts_str,
                                         const std::string& chips_str) {
  AnalysisOutput result;
  result.title = "ICM Analysis";

  std::vector<double> payouts, chips;
  auto parse = [](const std::string& s, std::vector<double>& out) {
    std::string cur;
    for (char c : s) {
      if (c == ',') {
        if (!cur.empty()) {
          out.push_back(std::stod(cur));
          cur.clear();
        }
      } else if (std::isdigit(c) || c == '.')
        cur += c;
    }
    if (!cur.empty()) out.push_back(std::stod(cur));
  };

  parse(payouts_str, payouts);
  parse(chips_str, chips);

  if (payouts.empty()) payouts = {50, 30, 20};
  if (chips.empty()) chips = {10000, 6000, 4000};

  auto icm_result = poker_engine::phase5::ICMCalculator::Calculate(
      payouts.data(), static_cast<int>(payouts.size()), chips.data(),
      static_cast<int>(chips.size()));

  std::ostringstream oss;
  oss << icm_result.ToString();
  result.content = oss.str();

  return result;
}

// ---- Batch ----
AnalysisOutput SolverManager::BatchAnalyze(const std::vector<std::string>& queries,
                                           const std::string& output_path) {
  AnalysisOutput result;
  result.title = "Batch Analysis (" + std::to_string(queries.size()) + " queries)";

  std::ostringstream oss;
  for (size_t i = 0; i < queries.size(); i++) {
    oss << "\n--- Query " << (i + 1) << ": " << queries[i].substr(0, 60) << " ---\n";
    oss << Analyze(queries[i]).content << "\n";
  }
  result.content = oss.str();

  if (!output_path.empty()) {
    std::ofstream out(output_path);
    if (out.is_open()) {
      out << result.content;
      out.close();
    }
  }

  return result;
}

}  // namespace phase11
}  // namespace poker_engine
