# AI / Agent Research

This engine doubles as an **open, reproducible research environment for
imperfect-information, multi-agent decision making** (No-Limit Texas Hold'em).
It is a real game engine — exact side-pot settlement, provably-fair dealing,
integer chip accounting — so agents are evaluated against production rules, not
a toy abstraction.

## The agent seam: `IAIEngine`

Every agent implements one interface (`phase13/include/poker_engine/network/ai_engine.h`,
ADR-004):

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
```

`Decide` receives a `DecisionRequest { game::Observation observation; int32_t
player_id; std::vector<GameAction> legal_actions; }` and returns a `GameAction`.
The `Observation` is a **redacted, per-player view**: it carries public table
state plus *only the viewer's own hole cards*. Opponents appear as `PlayerView`,
a struct that has no `hole_cards` field at all — so an agent cannot peek at
hidden information through the type system. The match runner supplies
`legal_actions` from `GameState::LegalActions(player_id)`, whose every entry is
guaranteed to pass `ActionValidator::Validate`.

### How to add an agent (3 steps)

1. Subclass `IAIEngine` and implement `Decide` — return a `GameAction`. Read
   `request.observation` (your own cards via `observation.MyHoleCards()`, your
   stack/bet via `observation.Me()`, the board via `observation.community`) and
   `request.legal_actions`; pick one, and for bets/raises set `amount` (in
   **cents**) between the minimum legal size and all-in.
2. Seed any RNG from `AIConfig::random_seed` so runs are reproducible.
3. Drop it into a match with `RunHeadsUp(agentA, agentB, MatchConfig{...})`.

`RandomAgent` (`arena/src/random_agent.cpp`) is the minimal reference: ~60 lines,
uniform over the legal set.

## Baselines

| Agent | What it is | Role |
|-------|------------|------|
| `RandomAgent` | uniform over legal actions | honest lower bound / floor |
| `AIEngine` (RuleBased) | hand-strength + pot-odds heuristics | scripted baseline |
| `AIEngine` (CfrModel) | CFR policy over a 169-bucket abstraction | solver baseline |

A meaningful agent must beat `RandomAgent` with statistical significance; a
strong one should also beat the rule-based baseline.

## The benchmark: `agent_bench`

Headless bot-vs-bot matches on the real engine, reporting a win rate with a
confidence interval:

```bash
# Sanity: random vs random has no edge (CI crosses zero)
./build/cli/agent_bench --a random --b random --hands 20000 --seed 1

# Signal: rule-based clearly beats random (lower CI bound > 0)
./build/cli/agent_bench --a rule --b random --hands 20000 --seed 1

# Solver baseline vs rule-based
./build/cli/agent_bench --a cfr --b rule --hands 20000 --cfr-model data/bot_policy.cfr

# Tighter CI for the same budget via duplicate (seat-rotation) pairing
./build/cli/agent_bench --a rule --b random --hands 20000 --seed 1 --duplicate

# Tighter CI via all-in EV adjustment (heads-up runout control variate)
./build/cli/agent_bench --a rule --b random --hands 20000 --seed 1 --aivat

# Multi-way (N-way) table
./build/cli/agent_bench --agents random,rule,random --hands 20000 --seed 1

# Parallel throughput (independent seed shards, pooled)
./build/cli/agent_bench --a random --b random --hands 1000000 --threads 8
```

Design notes:
- The **button alternates every hand**, so positional advantage cancels over a
  match.
- Each hand starts from a **fresh fixed stack** (default 200 bb), so per-hand
  results are independent samples.
- Every hand asserts **chip conservation** (`Σ net == 0`), a regression guard on
  the pot/side-pot settlement path.

### Metrics

- **mbb/100** — milli-big-blinds won per 100 hands. `1 bb = 1000 mbb`; a hand's
  result in mbb is `net_chips / big_blind * 1000`. This is the standard poker
  win-rate unit and is stake-independent.
- **95% CI** — normal-approximation half-width `1.96 · s/√n · 100`, where `s` is
  the sample standard deviation of the per-hand mbb result. An edge is credible
  when `mbb/100 − CI > 0`.

### Variance reduction (duplicate / seat rotation)

`--duplicate` replays each per-hand deck `k` times (`k` = number of agents),
rotating which agent occupies which seat, so **every agent plays every seat on
identical cards**. Because the deal luck is now symmetric across agents, it
cancels in agent 0's cross-rotation sum, and the confidence interval shrinks
sharply for the same number of *distinct* deals. This is honest duplicate
pairing (it cancels the luck of *who was dealt what*); it is not full AIVAT (it
does not also subtract a learned control variate on the community runout).

### All-in EV adjustment (runout control variate)

`--aivat` attacks the *other* big variance source in poker: the community
**runout after both players are all-in**. When a heads-up hand goes all-in, the
cards left to come are pure chance — a single cooler can swing a hand by a whole
stack. Instead of scoring such a hand by the cards that happened to fall, the
arena scores it by its **exact expected value**: with matched investment
`m = min(inv0, inv1)` and agent 0's exact equity `e0` on the pre-runout board,
the mbb sample becomes `m · (2·e0 − 1)`. Because `E[realized net] = m·(2e0 − 1)`,
this is an **unbiased** control variate — it changes only the variance of the
estimator, not its expectation. Equity is computed by exact enumeration on the
flop/turn and fixed-seed Monte Carlo preflop, so the result stays deterministic.

`net_by_seat` (and the `Σ net == 0` conservation check) always records the
**realized** chips; only the mbb/100 *sample* is EV-adjusted. `--aivat` composes
with `--duplicate` for further reduction. It applies to **heads-up NLHE only**;
for N-way tables it is ignored (reported as `no`).

### Multi-way (N-way) matches

`RunMatch(std::vector<IAIEngine*>, MatchConfig)` seats 2..`max_players` agents on
the real engine — full side-pot settlement applies. `--agents a,b,c,...` selects
the line-up; agent 0 is the one whose mbb/100 is reported. Chip conservation is
asserted every hand for any number of seats.

### Parallel throughput

`--threads T` splits the hand budget into `T` shards, each with its own agent
instances and a distinct sub-seed, then pools the sufficient statistics
(`n, Σx, Σx²`) to recover the exact same estimator. The arena library itself
stays single-threaded and side-effect free; parallelism lives in the CLI, one
independent match per thread. Results are deterministic for a fixed `--seed`
**and** a fixed `--threads` (changing the shard count re-partitions the seeds).

## Exploitability (honest boundary)

```bash
./build/cli/agent_bench --exploitability --cfr-model data/bot_policy.cfr
```

This reports the exploitability **recorded at training time**, measured **inside
the CFR abstraction** (169-bucket infosets, abstract bet sizes). It is *not*
exploitability in the full NLHE game, and it is *not* recomputed live (the
engine's live best-response is a placeholder). Treat it as an abstraction-level
convergence figure, not a game-theoretic guarantee against an unrestricted
opponent.

## Honest limitations

- **Redacted per-player observation.** `Decide` receives a `game::Observation`
  built by `GameState::ObserveFor(viewer_id)`: public table state plus only the
  viewer's own hole cards. Opponents' cards are not representable in the type, so
  an agent cannot read them. `OnHandComplete(const GameState&)` still hands over
  the full end-of-hand state — that is showdown information, which is public once
  the hand is over. (The trusted server and CFR *training* self-play still work
  directly on `GameState`; only the agent-facing `Decide` seam is redacted.)
- **Partial variance reduction, not full AIVAT.** `--duplicate` cancels the luck
  of *which seat was dealt which cards*, and `--aivat` replaces the all-in runout
  with its exact-EV control variate (unbiased, heads-up NLHE only). Neither yet
  subtracts an imaginary-observations term or a control variate on the *non-all-in*
  street-by-street public cards; full AIVAT-style variance reduction is future
  work.
- Odd hand counts leave a one-hand positional imbalance — negligible at the hand
  counts used for benchmarking.
