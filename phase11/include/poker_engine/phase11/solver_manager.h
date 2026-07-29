#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "poker_engine/phase11/decision_engine.h"
#include "poker_engine/phase11/fast_preflop_solver.h"
#include "poker_engine/phase5/equity_matrix.h"
#include "poker_engine/phase5/icm_calc.h"
#include "poker_engine/phase6/icfr_solver.h"
#include "poker_engine/phase7/cfr_plus_solver.h"
#include "poker_engine/phase9/variance_engine.h"

namespace poker_engine {
namespace phase11 {

// ========== 统一输出格式 ==========
struct AnalysisOutput {
  std::string title;
  std::string content;
  std::string json;
  double compute_time_seconds;

  std::string ToString() const;
};

// ========== 求解器类型 ==========
enum class SolverType {
  DECISION_ENGINE,
  MC_CFR,
  CFR_PLUS,
  ICFR,
  EQUITY_CALC,
  EQUITY_MATRIX,
  ICM,
  VARIANCE,
  PREFLOP_LUT,
  PREFLOP_CFR
};

// ========== 求解器管理器 (核心) ==========
class SolverManager {
 public:
  static SolverManager& Instance();

  void Initialize();

  // 统一分析入口
  AnalysisOutput Analyze(const std::string& query);

  // === 各求解器直接访问 ===
  DecisionEngine& GetDecisionEngine() { return decision_engine_; }
  FastPreflopSolver& GetFastPreflopSolver() { return fast_solver_; }
  poker_engine::phase7::CFRPlusSolver& GetCFRPlusSolver() { return *cfr_plus_; }
  poker_engine::phase6::ICFRSolver& GetICFRSolver() { return *icfr_; }
  poker_engine::phase5::EquityMatrixCalculator& GetEquityMatrixCalc() { return *eq_matrix_calc_; }
  poker_engine::phase9::VarianceEngine& GetVarianceEngine() { return variance_engine_; }

  // Tokenizer (shared with CLI tool)
  static std::vector<std::string> Tokenize(const std::string& text);

  // Batch
  AnalysisOutput BatchAnalyze(const std::vector<std::string>& queries,
                              const std::string& output_path = "");

 private:
  SolverManager() = default;
  SolverManager(const SolverManager&) = delete;
  SolverManager& operator=(const SolverManager&) = delete;

  DecisionEngine decision_engine_;
  FastPreflopSolver fast_solver_;
  std::unique_ptr<poker_engine::phase7::CFRPlusSolver> cfr_plus_;
  std::unique_ptr<poker_engine::phase6::ICFRSolver> icfr_;
  std::unique_ptr<poker_engine::phase5::EquityMatrixCalculator> eq_matrix_calc_;
  poker_engine::phase9::VarianceEngine variance_engine_;

  // Internal routing
  AnalysisOutput AnalyzeShortQuery(const std::string& query);
  AnalysisOutput AnalyzeLongQuery(const std::string& query);

  // Analysis helpers
  AnalysisOutput AnalyzeEquity(const std::string& hero_str, const std::string& villain_str,
                               const std::vector<std::string>& board_strs, AnalysisOutput& result);
  AnalysisOutput AnalyzePreflop(const std::string& position, AnalysisOutput& result);
  AnalysisOutput AnalyzeSingleHand(const std::string& hand, const std::string& position,
                                   AnalysisOutput& result);
  AnalysisOutput AnalyzeICM(const std::string& payouts_str, const std::string& chips_str);

  // Utility
  static bool IsHandDescription(const std::string& token);
  static bool IsPosition(const std::string& token);
  static bool IsNumeric(const std::string& token);
};

}  // namespace phase11
}  // namespace poker_engine
