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
| `CallStationAgent` | never bets/raises; checks/calls, folds only when forced | exploitable baseline (pays off, never bluffs) |
| `ManiacAgent` | maximum aggression; shoves all-in whenever legal | exploitable baseline (trap it) |
| `AIEngine` (RuleBased) | hand-strength + pot-odds heuristics | scripted baseline |
| `AIEngine` (CfrModel) | CFR policy over a 169-bucket abstraction | solver baseline |

`CallStationAgent` and `ManiacAgent` read only the legal action set (no hole
cards, no RNG — fully deterministic), so they are honest, reproducible sparring
partners. A meaningful agent must beat `RandomAgent` with statistical
significance and should crush the call-station and maniac; a strong one should
also beat the rule-based baseline.

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

# Tighter CI via per-street runout EV adjustment (heads-up chance control variate)
./build/cli/agent_bench --a rule --b random --hands 20000 --seed 1 --aivat

# Multi-way (N-way) table
./build/cli/agent_bench --agents random,rule,random --hands 20000 --seed 1

# Round-robin leaderboard across a field of agents
./build/cli/agent_bench --roundrobin --agents random,callstation,maniac,rule --hands 4000 --seed 1

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

### Runout EV adjustment (per-street chance control variate)

`--aivat` attacks the *other* big variance source in poker: the community
**runout once the matched money is committed**. In a heads-up hand, as soon as
one player is all-in and the other has called (with or without chips behind),
the matched pot `m = min(inv0, inv1)` is forced to showdown and every remaining
board card is pure chance — a single cooler can swing a hand by a whole stack.
Instead of scoring such a hand by the cards that happened to fall, the arena
subtracts a **chance control variate** at every forced deal: on each community
card dealt while the pot is a forced runout, it adds
`m · 2 · (e0(after) − e0(before))` to a per-hand correction and scores the hand
by `realized_net − Σ correction`. Here `e0` is agent 0's exact equity computed
from **both players' real hole cards** (readable in the trusted runner). Because
equity is a **martingale under a fair deal** — `E_next-card[e0(board′)] =
e0(board)` — each increment is conditionally zero-mean, so the estimator is
**unbiased for any agent** (no strategy knowledge needed) and only its variance
changes.

This generalizes the earlier all-in-only version: it also covers the
**one-player-all-in, caller-behind** case (dealt street by street), and it
telescopes exactly to the preflop double-all-in `m · (2·e0 − 1)`. Equity is
computed by exact enumeration on the flop/turn/river and fixed-seed Monte Carlo
preflop, so the result stays deterministic.

`net_by_seat` (and the `Σ net == 0` conservation check) always records the
**realized** chips; only the mbb/100 *sample* is EV-adjusted. `--aivat` composes
with `--duplicate` for further reduction. It applies to **heads-up NLHE only**;
for N-way tables it is ignored (reported as `no`).

### Multi-way (N-way) matches

`RunMatch(std::vector<IAIEngine*>, MatchConfig)` seats 2..`max_players` agents on
the real engine — full side-pot settlement applies. `--agents a,b,c,...` selects
the line-up; agent 0 is the one whose mbb/100 is reported. Chip conservation is
asserted every hand for any number of seats.

### Round-robin leaderboard

`--roundrobin` plays every unordered pair of `--agents` heads-up and prints a
mbb/100 matrix plus a ranked leaderboard. Because heads-up NLHE is zero-sum, each
pair is played **once**: the reported per-hand sample belongs to agent 0, and the
opponent's is its exact negation, so the matrix is antisymmetric by construction.
Each agent's samples are then pooled across all its opponents (using `n, Σx, Σx²`,
negating `Σx` for the matches where it sat as agent 1) to produce a global
mbb/100 ± CI. Every variance-reduction and throughput flag (`--duplicate`,
`--aivat`, `--threads`) applies to each pairing; results are deterministic for a
fixed `--seed` and `--threads`.

### Parallel throughput

`--threads T` splits the hand budget into `T` shards, each with its own agent
instances and a distinct sub-seed, then pools the sufficient statistics
(`n, Σx, Σx²`) to recover the exact same estimator. The arena library itself
stays single-threaded and side-effect free; parallelism lives in the CLI, one
independent match per thread. Results are deterministic for a fixed `--seed`
**and** a fixed `--threads` (changing the shard count re-partitions the seeds).

## Exploitability — live Local Best Response (LBR) lower bound

```bash
# Any black-box agent; larger mbb/100 = more exploitable.
./build/cli/agent_bench --exploitability --a callstation --hands 250 --seed 1
./build/cli/agent_bench --exploitability --a cfr --cfr-model data/bot_policy.cfr --hands 4000 --seed 1
```

`--exploitability` now runs a **live Local Best Response** (Lisý & Bowling, 2017)
against the chosen agent (`--a`) and reports LBR's realized win rate in mbb/100
with a 95% CI. This is a **lower bound on the opponent's true full-game
exploitability**: the real value is `>=` the reported number. LBR obtains the
opponent's per-hand behaviour by *counterfactual probing* — it asks a separate
instance of the same agent what it would do with each hypothetical villain
hole-card combo, then Bayes-filters its belief over the villain's range by
consistency with the observed action. The key correctness property: **the
measured win rate is a valid lower bound for any legal LBR policy** — the belief
model and bet-sizing EV only affect tightness, not validity.

Honest boundaries of this estimate:

- **It is a lower bound, not the exact value or an upper bound.** LBR
  value-/bluff-bets on **checked-to nodes** (`to_call == 0`), sizing its bet by
  the counterfactually probed fold probability and its equity vs the non-folding
  range; when **facing a bet** it only folds/calls by pot odds (it never
  re-raises). Because it explores only this restricted action set, it still
  *underestimates* exploitability — a fuller responder would prove a tighter bound.
- Betting is what lets LBR **punish over-calling**: it now provably exploits a
  passive **CallStation** (value betting its made hands), the opponent the earlier
  fold/call-only variant could not beat. It likewise crushes an over-aggressive
  **Maniac** by calling correctly and folding trash at pot odds.
- It is measured in the **full NLHE game** (not a CFR abstraction), and is
  deterministic for a fixed `--seed` (and `--threads`).

For reference, a trained `.cfr` model still carries a **training-time**
exploitability figure in its header, measured **inside the CFR abstraction**
(169-bucket infosets, abstract bet sizes). That is a different, non-live quantity
(an abstraction-level convergence figure, not a full-game guarantee); the CLI
prints it as a footnote when evaluating a `cfr` agent.

## Honest limitations

- **Redacted per-player observation.** `Decide` receives a `game::Observation`
  built by `GameState::ObserveFor(viewer_id)`: public table state plus only the
  viewer's own hole cards. Opponents' cards are not representable in the type, so
  an agent cannot read them. `OnHandComplete(const GameState&)` still hands over
  the full end-of-hand state — that is showdown information, which is public once
  the hand is over. (The trusted server and CFR *training* self-play still work
  directly on `GameState`; only the agent-facing `Decide` seam is redacted.)
- **Partial variance reduction, not full AIVAT.** `--duplicate` cancels the luck
  of *which seat was dealt which cards*, and `--aivat` subtracts a per-street
  chance control variate on every forced runout (unbiased, heads-up NLHE only),
  covering all-in-with-caller-behind cases. It does **not** yet subtract an
  action / imaginary-observations term (that needs the agent's known strategy),
  and pots decided purely by betting/folding (no all-in) get no control variate;
  full AIVAT-style variance reduction is future work.
- Odd hand counts leave a one-hand positional imbalance — negligible at the hand
  counts used for benchmarking.
