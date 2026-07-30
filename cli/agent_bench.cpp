// agent_bench — headless bot-vs-bot benchmark for the agent research seam.
//
// Examples:
//   agent_bench --a random --b random --hands 10000 --seed 1
//   agent_bench --a rule --b random --hands 20000 --seed 1 --duplicate
//   agent_bench --agents random,rule,random --hands 5000 --seed 1
//   agent_bench --a random --b random --hands 200000 --threads 8
//   agent_bench --cfr --a cfr --b rule --cfr-model data/bot_policy.cfr
//   agent_bench --roundrobin --agents random,callstation,maniac,rule --hands 4000 --seed 1
//   agent_bench --exploitability --a cfr --cfr-model data/bot_policy.cfr --hands 4000 --seed 1
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "poker_engine/arena/baseline_agents.h"
#include "poker_engine/arena/lbr.h"
#include "poker_engine/arena/match_runner.h"
#include "poker_engine/arena/random_agent.h"
#include "poker_engine/cfr/cfr_model.h"
#include "poker_engine/network/ai_engine.h"
#include "poker_engine/network/cfr_policy_store.h"

using poker_engine::arena::CallStationAgent;
using poker_engine::arena::ManiacAgent;
using poker_engine::arena::MatchConfig;
using poker_engine::arena::MatchResult;
using poker_engine::arena::RandomAgent;
using poker_engine::arena::RunMatch;
using poker_engine::network::AIConfig;
using poker_engine::network::AIStrategyType;
using poker_engine::network::CreateAIEngine;
using poker_engine::network::IAIEngine;

namespace {

void PrintUsage() {
  std::printf(
      "Usage: agent_bench --a <kind> --b <kind> [options]\n"
      "       agent_bench --agents <k1,k2,...> [options]\n"
      "       agent_bench --roundrobin --agents <k1,k2,...> [options]\n"
      "       agent_bench --exploitability --a <kind> [--cfr-model <path>]\n\n"
      "  <kind> is one of: random | callstation | maniac | rule | cfr\n\n"
      "Options:\n"
      "  --hands N          number of hands (default 10000)\n"
      "  --seed S           deterministic match seed (default 1)\n"
      "  --bb CENTS         big blind in cents (default 100 = $1)\n"
      "  --stack CENTS      starting stack per hand (default 200 big blinds)\n"
      "  --duplicate        variance reduction via seat rotation (duplicate poker)\n"
      "  --aivat            per-street runout EV adjustment (unbiased chance control variate; heads-up)\n"
      "  --roundrobin       play every pair of --agents and print a mbb/100 leaderboard\n"
      "  --exploitability   run live Local Best Response vs --a; prints a lower bound (LBR bets & re-raises by EV)\n"
      "  --threads T        parallel shards for throughput (default 1)\n"
      "  --cfr-model PATH   CFR policy weights (required for cfr agent)\n");
}

// Build one agent of the given kind. Does NOT load the CFR model — call
// EnsureCfrLoaded() once before spawning threads to avoid concurrent loads.
std::unique_ptr<IAIEngine> MakeAgent(const std::string& kind, uint64_t seed,
                                     const std::string& cfr_model) {
  AIConfig cfg;
  cfg.random_seed = static_cast<int>(seed & 0x7FFFFFFF);
  if (kind == "random") {
    cfg.name = "Random";
    return std::make_unique<RandomAgent>(cfg);
  }
  if (kind == "call" || kind == "callstation") {
    cfg.name = "CallStation";
    return std::make_unique<CallStationAgent>(cfg);
  }
  if (kind == "maniac" || kind == "allin") {
    cfg.name = "Maniac";
    return std::make_unique<ManiacAgent>(cfg);
  }
  if (kind == "rule") {
    cfg.name = "RuleBased";
    cfg.strategy = AIStrategyType::RuleBased;
    return CreateAIEngine(cfg);
  }
  if (kind == "cfr") {
    cfg.name = "CFR";
    cfg.strategy = AIStrategyType::CfrModel;
    cfg.model_path = cfr_model;
    return CreateAIEngine(cfg);
  }
  std::fprintf(stderr, "error: unknown agent kind '%s'\n", kind.c_str());
  return nullptr;
}

bool EnsureCfrLoaded(const std::vector<std::string>& kinds, const std::string& cfr_model) {
  bool needs_cfr = false;
  for (const auto& k : kinds) needs_cfr = needs_cfr || (k == "cfr");
  if (!needs_cfr) return true;
  if (cfr_model.empty()) {
    std::fprintf(stderr, "error: --cfr-model is required for the cfr agent\n");
    return false;
  }
  if (!poker_engine::network::CfrPolicyStore::Instance().LoadFromFile(cfr_model)) {
    std::fprintf(stderr, "error: failed to load CFR model: %s\n", cfr_model.c_str());
    return false;
  }
  return true;
}

std::vector<std::string> SplitCsv(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t comma = s.find(',', start);
    if (comma == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, comma - start));
    start = comma + 1;
  }
  return out;
}

// mbb/100 + CI from pooled sufficient statistics (defined below).
void StatsFromSuff(long long n, double sum, double sumsq, double* mbb, double* ci95);

// Live Local Best Response (LBR) exploitability lower bound. Plays LBR against
// the black-box opponent `kind` for `hands` hands (sharded across `threads`),
// pools the per-hand mbb sufficient statistics, and reports LBR's mbb/100 ± CI
// — a valid LOWER BOUND on the opponent's true full-game exploitability.
int RunLbrExploitability(const std::string& kind, int hands, uint64_t seed, int threads,
                         int64_t bb, int64_t stack, const std::string& cfr_model) {
  if (hands <= 0 || bb <= 0) {
    std::fprintf(stderr, "error: --hands and --bb must be positive\n");
    return 1;
  }
  if (!EnsureCfrLoaded({kind}, cfr_model)) return 1;
  if (threads < 1) threads = 1;
  if (threads > hands) threads = hands;

  poker_engine::arena::LbrConfig base;
  base.table.small_blind = bb / 2;
  base.table.big_blind = bb;
  base.table.ante = 0;
  base.starting_stack = stack;

  const int base_hands = hands / threads;
  const int remainder = hands % threads;

  std::vector<poker_engine::arena::LbrResult> shard(threads);
  std::vector<char> shard_ok(threads, 0);
  std::vector<std::thread> pool;

  for (int t = 0; t < threads; ++t) {
    const int shard_hands = base_hands + (t < remainder ? 1 : 0);
    poker_engine::arena::LbrConfig cfg = base;
    cfg.hands = shard_hands;
    cfg.seed = seed + static_cast<uint64_t>(t);
    pool.emplace_back([&, t, cfg, shard_hands]() {
      const uint64_t live_seed = cfg.seed * 131u + 1u;
      const uint64_t probe_seed = cfg.seed * 131u + 977u;
      auto live = MakeAgent(kind, live_seed, cfr_model);
      auto probe = MakeAgent(kind, probe_seed, cfr_model);
      if (!live || !probe || shard_hands <= 0) {
        shard_ok[t] = (shard_hands <= 0) ? 1 : 0;
        return;
      }
      shard[t] = poker_engine::arena::RunLbr(*live, *probe, cfg);
      shard_ok[t] = 1;
    });
  }
  for (auto& th : pool) th.join();

  long long n = 0;
  double sum = 0.0, sumsq = 0.0;
  long long hands_played = 0;
  bool conserved = true;
  for (int t = 0; t < threads; ++t) {
    if (!shard_ok[t]) {
      std::fprintf(stderr, "error: LBR shard %d failed (bad agent kind?)\n", t);
      return 1;
    }
    n += shard[t].sample_n;
    sum += shard[t].sample_sum;
    sumsq += shard[t].sample_sumsq;
    hands_played += shard[t].hands_played;
    if (!shard[t].chips_conserved) conserved = false;
  }

  double mbb = 0.0, ci95 = 0.0;
  StatsFromSuff(n, sum, sumsq, &mbb, &ci95);

  std::printf("Local Best Response (LBR) exploitability lower bound\n");
  std::printf("%-22s %15s\n", "opponent", kind.c_str());
  std::printf("%-22s %15llu\n", "seed", static_cast<unsigned long long>(seed));
  std::printf("%-22s %15lld\n", "big blind (cents)", static_cast<long long>(bb));
  std::printf("%-22s %15d\n", "threads", threads);
  std::printf("%-22s %15lld\n", "hands played", hands_played);
  std::printf("%-22s %15.2f\n", "LBR mbb/100", mbb);
  std::printf("%-22s %15.2f\n", "95% CI (+/-)", ci95);
  std::printf("%-22s %15s\n", "chips conserved", conserved ? "yes" : "NO");
  std::printf(
      "\nNOTE: LBR value-/bluff-bets on checked-to nodes (to_call==0) and also\n"
      "      re-raises when facing a bet whenever an EV estimate beats calling,\n"
      "      so this is a TIGHTER but still valid LOWER BOUND on the opponent's\n"
      "      true full-game exploitability: the real value is >= this. Larger\n"
      "      mbb/100 = more exploitable. It is measured in the FULL NLHE game\n"
      "      (not a CFR abstraction).\n");
  if (kind == "cfr" && !cfr_model.empty()) {
    auto info = poker_engine::cfr::CFRModelIO::GetInfo(cfr_model);
    if (info) {
      std::printf(
          "      (For reference, the .cfr header records a TRAINING-time value of\n"
          "      %.6f measured WITHIN the CFR abstraction — a different, non-live\n"
          "      quantity from the LBR lower bound above.)\n",
          info->exploitability);
    }
  }
  return conserved ? 0 : 2;
}

// Run one shard: build its own agents (so threads share no mutable state) and
// play `hands` hands with the given match seed.
MatchResult RunShard(const std::vector<std::string>& kinds, const MatchConfig& base,
                     uint64_t match_seed, const std::string& cfr_model, bool* ok) {
  std::vector<std::unique_ptr<IAIEngine>> owned;
  std::vector<IAIEngine*> agents;
  for (size_t i = 0; i < kinds.size(); ++i) {
    const uint64_t agent_seed = match_seed * 131u + static_cast<uint64_t>(i) * 977u + 1u;
    auto a = MakeAgent(kinds[i], agent_seed, cfr_model);
    if (!a) {
      *ok = false;
      return MatchResult{};
    }
    agents.push_back(a.get());
    owned.push_back(std::move(a));
  }
  MatchConfig cfg = base;
  cfg.seed = match_seed;
  MatchResult r = RunMatch(agents, cfg);
  *ok = true;
  return r;
}

// Run `hands` hands for `kinds` across `threads` independent shards (distinct
// sub-seeds) and pool the sufficient statistics into a single MatchResult, so a
// threaded run reproduces the exact estimator of a single-threaded one.
MatchResult PooledMatch(const std::vector<std::string>& kinds, const MatchConfig& base,
                        int hands, int threads, uint64_t seed, const std::string& cfr_model,
                        bool* ok) {
  const int k = static_cast<int>(kinds.size());
  if (threads < 1) threads = 1;
  if (threads > hands) threads = hands;

  std::vector<MatchResult> shard_results(threads);
  std::vector<char> shard_ok(threads, 0);
  std::vector<std::thread> pool;
  const int base_hands = hands / threads;
  const int remainder = hands % threads;

  for (int t = 0; t < threads; ++t) {
    const int shard_hands = base_hands + (t < remainder ? 1 : 0);
    MatchConfig cfg = base;
    cfg.hands = shard_hands;
    const uint64_t match_seed = seed + static_cast<uint64_t>(t);
    pool.emplace_back([&, t, cfg, match_seed]() {
      bool sok = false;
      MatchResult r = RunShard(kinds, cfg, match_seed, cfr_model, &sok);
      shard_results[t] = r;
      shard_ok[t] = sok ? 1 : 0;
    });
  }
  for (auto& th : pool) th.join();

  MatchResult agg;
  agg.net_by_seat.assign(k, 0);
  agg.big_blind = static_cast<double>(base.table.big_blind);
  agg.reps = base.duplicate ? k : 1;
  agg.variance_reduced = base.duplicate;
  double sum = 0.0, sumsq = 0.0;
  long long n = 0;
  for (int t = 0; t < threads; ++t) {
    if (!shard_ok[t]) {
      *ok = false;
      return agg;
    }
    const MatchResult& r = shard_results[t];
    for (int a = 0; a < k; ++a) agg.net_by_seat[a] += r.net_by_seat[a];
    sum += r.sample_sum;
    sumsq += r.sample_sumsq;
    n += r.sample_n;
    agg.hands_played += r.hands_played;
    if (!r.chips_conserved) agg.chips_conserved = false;
    agg.adjusted_hands += r.adjusted_hands;
    if (r.aivat_applied) agg.aivat_applied = true;
  }
  agg.sample_n = n;
  agg.sample_sum = sum;
  agg.sample_sumsq = sumsq;
  if (n > 0) {
    const double mean = sum / static_cast<double>(n);
    agg.mbb_per_100 = mean * 100.0;
    if (n > 1) {
      const double var = (sumsq - static_cast<double>(n) * mean * mean) / static_cast<double>(n - 1);
      const double se = std::sqrt(var / static_cast<double>(n));
      agg.ci95 = 1.96 * se * 100.0;
    }
  }
  *ok = true;
  return agg;
}

// mbb/100 + CI from pooled sufficient statistics (n, Σx, Σx²).
void StatsFromSuff(long long n, double sum, double sumsq, double* mbb, double* ci95) {
  *mbb = 0.0;
  *ci95 = 0.0;
  if (n > 0) {
    const double mean = sum / static_cast<double>(n);
    *mbb = mean * 100.0;
    if (n > 1) {
      const double var = (sumsq - static_cast<double>(n) * mean * mean) / static_cast<double>(n - 1);
      const double se = std::sqrt(var / static_cast<double>(n));
      *ci95 = 1.96 * se * 100.0;
    }
  }
}

int RunRoundRobin(const std::vector<std::string>& kinds, const MatchConfig& base, int hands,
                  int threads, uint64_t seed, const std::string& cfr_model) {
  const int k = static_cast<int>(kinds.size());

  // Pairwise mbb/100 matrix (row = agent, col = opponent) and per-agent pooled
  // sufficient statistics. Heads-up is zero-sum, so agent1's per-hand sample is
  // the negation of agent0's: one match per unordered pair suffices for both.
  std::vector<std::vector<double>> mbb_matrix(k, std::vector<double>(k, 0.0));
  std::vector<long long> agent_n(k, 0);
  std::vector<double> agent_sum(k, 0.0), agent_sumsq(k, 0.0);
  std::vector<long long> agent_hands(k, 0);

  bool all_conserved = true;
  long long total_adjusted = 0;
  bool any_aivat = false;

  int pair_index = 0;
  for (int i = 0; i < k; ++i) {
    for (int j = i + 1; j < k; ++j) {
      std::vector<std::string> pair = {kinds[i], kinds[j]};
      const uint64_t pair_seed = seed + static_cast<uint64_t>(pair_index) * 2654435761ull;
      ++pair_index;
      bool ok = false;
      MatchResult r = PooledMatch(pair, base, hands, threads, pair_seed, cfr_model, &ok);
      if (!ok) {
        std::fprintf(stderr, "error: match %s vs %s failed\n", kinds[i].c_str(), kinds[j].c_str());
        return 1;
      }
      const std::int64_t net_total = r.net_by_seat[0] + r.net_by_seat[1];
      if (!r.chips_conserved || net_total != 0) all_conserved = false;
      total_adjusted += r.adjusted_hands;
      if (r.aivat_applied) any_aivat = true;

      mbb_matrix[i][j] = r.mbb_per_100;
      mbb_matrix[j][i] = -r.mbb_per_100;

      // agent i is agent0 (samples as-is); agent j is agent1 (negate sum).
      agent_n[i] += r.sample_n;
      agent_sum[i] += r.sample_sum;
      agent_sumsq[i] += r.sample_sumsq;
      agent_hands[i] += r.hands_played;

      agent_n[j] += r.sample_n;
      agent_sum[j] += -r.sample_sum;
      agent_sumsq[j] += r.sample_sumsq;
      agent_hands[j] += r.hands_played;
    }
  }

  std::printf("round-robin: %d agents  hands/pair: %d  seed: %llu  bb: %lld cents  threads: %d\n",
              k, hands, static_cast<unsigned long long>(seed),
              static_cast<long long>(base.table.big_blind), threads);
  std::printf("duplicate: %s   aivat: %s\n\n", base.duplicate ? "yes" : "no",
              base.aivat ? "yes" : "no");

  // Pairwise matrix: cell [row][col] = row's mbb/100 vs col.
  std::printf("mbb/100 matrix (row vs col):\n");
  std::printf("%-14s", "");
  for (int j = 0; j < k; ++j) std::printf("%12d", j);
  std::printf("\n");
  for (int i = 0; i < k; ++i) {
    char label[16];
    std::snprintf(label, sizeof(label), "[%d]%s", i, kinds[i].c_str());
    std::printf("%-14.14s", label);
    for (int j = 0; j < k; ++j) {
      if (i == j) std::printf("%12s", "--");
      else std::printf("%12.1f", mbb_matrix[i][j]);
    }
    std::printf("\n");
  }

  // Leaderboard: pooled mbb/100 + CI per agent, ranked descending.
  std::vector<int> order(k);
  std::iota(order.begin(), order.end(), 0);
  std::vector<double> mbb(k, 0.0), ci(k, 0.0);
  for (int a = 0; a < k; ++a) StatsFromSuff(agent_n[a], agent_sum[a], agent_sumsq[a], &mbb[a], &ci[a]);
  std::sort(order.begin(), order.end(), [&](int x, int y) { return mbb[x] > mbb[y]; });

  std::printf("\nleaderboard (pooled across all opponents):\n");
  std::printf("%-4s %-14s %14s %14s %12s\n", "rank", "agent", "mbb/100", "95% CI (+/-)", "hands");
  for (int rank = 0; rank < k; ++rank) {
    const int a = order[rank];
    char label[16];
    std::snprintf(label, sizeof(label), "[%d]%s", a, kinds[a].c_str());
    std::printf("%-4d %-14.14s %14.2f %14.2f %12lld\n", rank + 1, label, mbb[a], ci[a],
                agent_hands[a]);
  }

  std::printf("\n%-22s %15s\n", "aivat (runout EV)", any_aivat ? "yes" : "no");
  std::printf("%-22s %15lld\n", "adjusted hands", total_adjusted);
  std::printf("%-22s %15s\n", "chips conserved", all_conserved ? "yes" : "NO");

  return all_conserved ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string kind_a = "random";
  std::string kind_b = "random";
  std::string agents_csv;
  std::string cfr_model;
  int hands = 10000;
  uint64_t seed = 1;
  int64_t bb = 100;   // $1.00
  int64_t stack = 0;  // 0 => 200bb
  bool exploitability = false;
  bool duplicate = false;
  bool aivat = false;
  bool roundrobin = false;
  int threads = 1;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* flag) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s expects a value\n", flag);
        std::exit(1);
      }
      return argv[++i];
    };
    if (arg == "--a") kind_a = next("--a");
    else if (arg == "--b") kind_b = next("--b");
    else if (arg == "--agents") agents_csv = next("--agents");
    else if (arg == "--hands") hands = std::atoi(next("--hands"));
    else if (arg == "--seed") seed = std::strtoull(next("--seed"), nullptr, 10);
    else if (arg == "--bb") bb = std::atoll(next("--bb"));
    else if (arg == "--stack") stack = std::atoll(next("--stack"));
    else if (arg == "--duplicate") duplicate = true;
    else if (arg == "--aivat") aivat = true;
    else if (arg == "--roundrobin") roundrobin = true;
    else if (arg == "--threads") threads = std::atoi(next("--threads"));
    else if (arg == "--cfr-model") cfr_model = next("--cfr-model");
    else if (arg == "--exploitability") exploitability = true;
    else if (arg == "--help" || arg == "-h") { PrintUsage(); return 0; }
    else { std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str()); PrintUsage(); return 1; }
  }

  if (exploitability)
    return RunLbrExploitability(kind_a, hands, seed, threads, bb, stack, cfr_model);

  if (hands <= 0 || bb <= 0) {
    std::fprintf(stderr, "error: --hands and --bb must be positive\n");
    return 1;
  }

  std::vector<std::string> kinds;
  if (!agents_csv.empty()) {
    kinds = SplitCsv(agents_csv);
  } else {
    kinds = {kind_a, kind_b};
  }
  if (kinds.size() < 2) {
    std::fprintf(stderr, "error: need at least 2 agents\n");
    return 1;
  }
  if (threads < 1) threads = 1;
  if (threads > hands) threads = hands;

  if (!EnsureCfrLoaded(kinds, cfr_model)) return 1;

  MatchConfig base;
  base.table.small_blind = bb / 2;
  base.table.big_blind = bb;
  base.table.ante = 0;
  base.starting_stack = stack;
  base.duplicate = duplicate;
  base.aivat = aivat;

  if (roundrobin) return RunRoundRobin(kinds, base, hands, threads, seed, cfr_model);

  std::printf("agents: ");
  for (size_t i = 0; i < kinds.size(); ++i)
    std::printf("%s%s", i ? "," : "", kinds[i].c_str());
  std::printf("   hands: %d  seed: %llu  bb: %lld cents  threads: %d  duplicate: %s\n", hands,
              static_cast<unsigned long long>(seed), static_cast<long long>(bb), threads,
              duplicate ? "yes" : "no");

  const int k = static_cast<int>(kinds.size());
  bool ok = false;
  MatchResult agg = PooledMatch(kinds, base, hands, threads, seed, cfr_model, &ok);
  if (!ok) return 1;

  std::int64_t net_total = 0;
  for (int a = 0; a < k; ++a) net_total += agg.net_by_seat[a];

  std::printf("\n");
  for (int a = 0; a < k; ++a) {
    std::string label = (a == 0) ? "net chips A (cents)"
                                  : ("net chips " + std::to_string(a) + " (cents)");
    std::printf("%-22s %15lld\n", label.c_str(), static_cast<long long>(agg.net_by_seat[a]));
  }
  std::printf("%-22s %15.2f\n", "mbb/100 (agent A)", agg.mbb_per_100);
  std::printf("%-22s %15.2f\n", "95% CI (+/-)", agg.ci95);
  std::printf("%-22s %15d\n", "hands played", agg.hands_played);
  std::printf("%-22s %15d\n", "rotations (reps)", agg.reps);
  std::printf("%-22s %15s\n", "variance reduced", agg.variance_reduced ? "yes" : "no");
  std::printf("%-22s %15s\n", "aivat (runout EV)", agg.aivat_applied ? "yes" : "no");
  std::printf("%-22s %15lld\n", "adjusted hands",
              static_cast<long long>(agg.adjusted_hands));
  std::printf("%-22s %15s\n", "chips conserved",
              agg.chips_conserved && (net_total == 0) ? "yes" : "NO");

  if (!agg.chips_conserved || net_total != 0) return 2;
  return 0;
}
