// agent_bench — headless bot-vs-bot benchmark for the agent research seam.
//
// Examples:
//   agent_bench --a random --b random --hands 10000 --seed 1
//   agent_bench --a rule --b random --hands 20000 --seed 1 --duplicate
//   agent_bench --agents random,rule,random --hands 5000 --seed 1
//   agent_bench --a random --b random --hands 200000 --threads 8
//   agent_bench --cfr --a cfr --b rule --cfr-model data/bot_policy.cfr
//   agent_bench --exploitability --cfr-model data/bot_policy.cfr
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "poker_engine/arena/match_runner.h"
#include "poker_engine/arena/random_agent.h"
#include "poker_engine/cfr/cfr_model.h"
#include "poker_engine/network/ai_engine.h"
#include "poker_engine/network/cfr_policy_store.h"

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
      "       agent_bench --exploitability --cfr-model <path>\n\n"
      "  <kind> is one of: random | rule | cfr\n\n"
      "Options:\n"
      "  --hands N          number of hands (default 10000)\n"
      "  --seed S           deterministic match seed (default 1)\n"
      "  --bb CENTS         big blind in cents (default 100 = $1)\n"
      "  --stack CENTS      starting stack per hand (default 200 big blinds)\n"
      "  --duplicate        variance reduction via seat rotation (duplicate poker)\n"
      "  --aivat            all-in EV adjustment (unbiased runout control variate; heads-up)\n"
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

int RunExploitability(const std::string& cfr_model) {
  if (cfr_model.empty()) {
    std::fprintf(stderr, "error: --exploitability requires --cfr-model <path>\n");
    return 1;
  }
  auto info = poker_engine::cfr::CFRModelIO::GetInfo(cfr_model);
  if (!info) {
    std::fprintf(stderr, "error: failed to read CFR model header: %s\n", cfr_model.c_str());
    return 1;
  }
  std::printf("model:            %s\n", cfr_model.c_str());
  std::printf("format version:   %u\n", info->version);
  std::printf("infoset nodes:    %llu\n", static_cast<unsigned long long>(info->node_count));
  std::printf("exploitability:   %.6f\n", info->exploitability);
  std::printf(
      "NOTE: this is the value recorded at TRAINING time, measured within the\n"
      "      CFR abstraction (169-bucket infosets, abstract bet sizes) — NOT\n"
      "      exploitability in the full NLHE game.\n");
  return 0;
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
    else if (arg == "--threads") threads = std::atoi(next("--threads"));
    else if (arg == "--cfr-model") cfr_model = next("--cfr-model");
    else if (arg == "--exploitability") exploitability = true;
    else if (arg == "--help" || arg == "-h") { PrintUsage(); return 0; }
    else { std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str()); PrintUsage(); return 1; }
  }

  if (exploitability) return RunExploitability(cfr_model);

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

  std::printf("agents: ");
  for (size_t i = 0; i < kinds.size(); ++i)
    std::printf("%s%s", i ? "," : "", kinds[i].c_str());
  std::printf("   hands: %d  seed: %llu  bb: %lld cents  threads: %d  duplicate: %s\n", hands,
              static_cast<unsigned long long>(seed), static_cast<long long>(bb), threads,
              duplicate ? "yes" : "no");

  // Split hands across threads; each shard gets a distinct match seed so its
  // deals are independent, then pool the sufficient statistics.
  const int k = static_cast<int>(kinds.size());
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
      bool ok = false;
      MatchResult r = RunShard(kinds, cfg, match_seed, cfr_model, &ok);
      shard_results[t] = r;
      shard_ok[t] = ok ? 1 : 0;
    });
  }
  for (auto& th : pool) th.join();

  // Pool shards.
  MatchResult agg;
  agg.net_by_seat.assign(k, 0);
  agg.big_blind = static_cast<double>(bb);
  agg.reps = duplicate ? k : 1;
  agg.variance_reduced = duplicate;
  double sum = 0.0, sumsq = 0.0;
  long long n = 0;
  for (int t = 0; t < threads; ++t) {
    if (!shard_ok[t]) return 1;
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
  if (n > 0) {
    const double mean = sum / static_cast<double>(n);
    agg.mbb_per_100 = mean * 100.0;
    if (n > 1) {
      const double var = (sumsq - static_cast<double>(n) * mean * mean) / static_cast<double>(n - 1);
      const double se = std::sqrt(var / static_cast<double>(n));
      agg.ci95 = 1.96 * se * 100.0;
    }
  }

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
  std::printf("%-22s %15s\n", "aivat (all-in EV)", agg.aivat_applied ? "yes" : "no");
  std::printf("%-22s %15lld\n", "adjusted hands",
              static_cast<long long>(agg.adjusted_hands));
  std::printf("%-22s %15s\n", "chips conserved",
              agg.chips_conserved && (net_total == 0) ? "yes" : "NO");

  if (!agg.chips_conserved || net_total != 0) return 2;
  return 0;
}
