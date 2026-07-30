# Build an Agent

This project is an **imperfect-information, multi-agent environment** with a
deterministic realtime runtime. No-Limit Texas Hold'em is the reference game:
hidden information (hole cards), multiple self-interested players, a large
stochastic state space, and an exact, reproducible rule set. That makes it a
practical arena for testing decision-making agents — scripted heuristics, CFR
solvers, RL policies, or LLMs — against production rules rather than a toy model.

This page is the **front door**: what the agent contract is, how to add your
own, and how to evaluate it. For metric definitions, variance reduction, and
honest statistical boundaries, see **[ai-research.md](ai-research.md)**.

## Why this environment

| What an agent needs | What this project provides |
|---|---|
| An environment to act in | `GameState` — the real engine (exact side-pot settlement, integer chips, provably-fair dealing) |
| A legal action space | `GameState::LegalActions(id)` — every entry is guaranteed to pass `ActionValidator` |
| Imperfect information | `GameState::ObserveFor(id)` returns a **redacted** `Observation`: your cards + public state only |
| A standard agent interface | `network::IAIEngine` (`Decide(DecisionRequest) → DecisionResponse`) |
| Multi-agent play | `arena::RunMatch(std::vector<IAIEngine*>, cfg)` — 2..N seats on the real engine |
| Reproducible evaluation | `agent_bench`: mbb/100 + 95% CI, chip-conservation asserted every hand |
| Ready-made opponents | baseline agents (`random`, `callstation`, `maniac`, `rule`, `cfr`) + a round-robin leaderboard |
| Variance reduction | duplicate (seat-rotation) pairing + per-street runout EV adjustment (`--duplicate`, `--aivat`) |
| A solver reference point | CFR policy baseline + abstraction-level exploitability |

## The agent contract

Every agent implements one interface
(`phase13/include/poker_engine/network/ai_engine.h`):

```cpp
class IAIEngine {
 public:
  virtual void Initialize(const AIConfig& config) = 0;
  virtual DecisionResponse Decide(const DecisionRequest& request) = 0;
  virtual void OnHandComplete(const GameState& final_state) = 0;
  virtual bool ReloadModel(const std::string& model_path) = 0;
  virtual std::string Name() const = 0;
  virtual AIDifficulty Difficulty() const = 0;
};

struct DecisionRequest {
  game::Observation observation;          // redacted per-player view
  int32_t player_id;
  std::vector<GameAction> legal_actions;  // all validator-legal
};
```

The `Observation` carries public table state plus **only the viewer's own hole
cards**. Opponents appear as `PlayerView`, a struct with **no `hole_cards` field
at all** — so an agent cannot read hidden information through the type system.
`OnHandComplete` hands over the full end-of-hand `GameState` (showdown info,
which is public once the hand is over).

## How to add an agent (3 steps)

1. **Implement `IAIEngine`.** In `Decide`, read `request.observation` (your cards
   via `observation.MyHoleCards()`, stack/bet via `observation.Me()`, the board
   via `observation.community`) and `request.legal_actions`; return a
   `GameAction`. For bet/raise, set `amount` in **cents** between the legal
   minimum and all-in. Seed any RNG from `AIConfig::random_seed` for
   reproducibility.
2. **Match it.** `arena::RunHeadsUp(a, b, MatchConfig{...})` or
   `arena::RunMatch({&a, &b, &c}, cfg)` for N-way.
3. **Evaluate it.** Run `agent_bench` (below) to get mbb/100 with a confidence
   interval, and confirm it beats the `RandomAgent` floor with significance.

Two references live in-tree:

- **Minimal:** `arena::RandomAgent` (`arena/src/random_agent.cpp`) — ~60 lines,
  uniform over the legal set. The smallest possible `IAIEngine`.
- **Real, optional:** `examples/llm-agent/` — an `LlmAgent` that renders the
  redacted observation into a prompt, calls an OpenAI-compatible endpoint over
  HTTPS, and maps the reply back onto a legal action. It is env-gated
  (`OPENAI_API_KEY`) and falls back to a safe passive baseline when no key is
  present, so it never crashes a match. Built only with `-DBUILD_EXAMPLES=ON`.

## Evaluate: `agent_bench`

```bash
# random vs random → no edge (CI crosses zero)
./build/cli/agent_bench --a random --b random --hands 20000 --seed 1

# rule-based vs random → clear, significant edge (lower CI bound > 0)
./build/cli/agent_bench --a rule --b random --hands 20000 --seed 1

# variance reduction: seat-rotation duplicate + per-street runout EV adjustment
./build/cli/agent_bench --a rule --b random --hands 4000 --seed 1 --duplicate --aivat

# N-way table, parallelized for throughput
./build/cli/agent_bench --agents random,rule,cfr --hands 20000 --threads 8 \
    --cfr-model data/bot_policy.cfr
```

- **mbb/100** — milli-big-blinds per 100 hands, the stake-independent win rate.
- **95% CI** — an edge is credible when `mbb/100 − CI > 0`.
- Every hand asserts **chip conservation** (`Σ net == 0`).

## Baseline agents

Five honest opponents ship in-tree so a new agent has something to beat out of
the box. `callstation` and `maniac` are the classic *exploitable* sparring
partners: they read only the legal action set (no hole cards, no RNG — fully
deterministic), so any competent agent should crush them.

| kind | strategy | known weakness |
|---|---|---|
| `random` | uniform over legal actions | no strategy at all — the floor |
| `callstation` | never bets/raises; checks or calls, folds only when it must | pay it off; it never bluffs and never folds |
| `maniac` | maximum aggression; shoves all-in whenever it can | trap with strong hands, fold junk |
| `rule` | hand-strength heuristics (`AIEngine` RuleBased) | static, no board-texture nuance |
| `cfr` | trained CFR policy (needs `--cfr-model`) | abstraction-level, not full-game GTO |

## Round-robin leaderboard

`--roundrobin` plays every pair of `--agents` and prints a mbb/100 matrix plus a
ranked leaderboard. Because heads-up is zero-sum, each pair is played once and
both sides are read off the same match (agent B's rate is the negation of agent
A's), then each agent's samples are pooled across all its opponents for a global
mbb/100 ± CI.

```bash
./build/cli/agent_bench --roundrobin \
    --agents random,callstation,maniac,rule --hands 4000 --seed 1
```

All the variance-reduction and throughput flags (`--duplicate`, `--aivat`,
`--threads`) apply to every pairing. Results are deterministic for a fixed
`--seed` **and** a fixed `--threads`.

## Honest boundaries

- **Redacted observation, trusted runner.** Agents see only their own cards
  through `Decide`. The match runner itself is a trusted component and works on
  the full `GameState`.
- **Partial variance reduction.** `--duplicate` cancels deal luck; `--aivat` adds
  an unbiased per-street chance control variate on every heads-up forced runout
  (any-street all-in, incl. caller-behind). This is **not** full AIVAT (no
  action / imaginary-observations term; pots decided purely by betting get no
  variate).
- **Exploitability is abstraction-level.** The CFR exploitability figure is
  measured inside the training abstraction, not the full NLHE game.

See **[ai-research.md](ai-research.md)** for the full methodology.
