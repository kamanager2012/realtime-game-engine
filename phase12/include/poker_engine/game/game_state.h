#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "poker_engine/game/action.h"
#include "poker_engine/game/dealer.h"
#include "poker_engine/game/observation.h"
#include "poker_engine/game/player_state.h"
#include "poker_engine/game/pot_manager.h"
#include "poker_engine/game/showdown_evaluator.h"

namespace poker_engine::game {

struct GameEvent {
  enum Type {
    HAND_STARTED,
    CARDS_DEALT,
    COMMUNITY_DEALT,
    ACTION_TAKEN,
    BETTING_ROUND_END,
    SHOWDOWN,
    PAYOUT,
    HAND_COMPLETE,
    PLAYER_JOINED,
    PLAYER_LEFT,
    ERROR
  };
  Type type;
  std::string message;
  int32_t player_id = -1;
  ActionType action = ActionType::FOLD;
  Chips amount = 0;
  int street = -1;
  Chips pot_after = 0;
  bool has_action = false;
};

struct TableConfig {
  std::string table_name = "Table 1";
  GameType game_type = GameType::NLHE;  // NLHE, PLO, PLO5
  int max_players = 9;
  Chips small_blind = 50;
  Chips big_blind = 1000000;
  Chips min_buy_in = 100000;
  Chips max_buy_in = 1000000;
  Chips ante = 0;
  int button_seat = 0;
  int hand_timeout_seconds = 30;
  bool is_tournament = false;
  int HoleCardsPerPlayer() const {
    switch (game_type) {
      case GameType::PLO: case GameType::PLO_HILO: return 4;
      case GameType::PLO5: return 5;
      default: return 2;  // NLHE
    }
  }
};

class GameState {
 public:
  explicit GameState(const TableConfig& config = TableConfig());

  // === Table management ===
  bool AddPlayer(int32_t player_id, const std::string& name, Chips chips);
  bool AddPlayerAtSeat(int32_t player_id, const std::string& name, Chips chips, uint8_t seat);
  bool RemovePlayer(int32_t player_id);
  bool SitDown(int32_t player_id, uint8_t seat);
  bool StandUp(int32_t player_id);

  // Cash-out leave (explicit player intent). If no hand is live, vacates the
  // seat immediately and returns the stack. If a hand is live: an active
  // player is folded (their invested chips stay in the pot) and ALL leavers
  // keep the seat until hand end — the stack (plus any all-in winnings) is
  // settled then via VacateLeavingPlayers(). Returns std::nullopt if the
  // player is not seated; otherwise the stack cashable RIGHT NOW (0 when the
  // settlement is deferred to hand end).
  std::optional<Chips> RequestCashOut(int32_t player_id);

  // Sweep seats marked `leaving` between hands; returns each vacated
  // player's id and final stack so the caller can credit their wallet.
  // No-op while a hand is in progress.
  std::vector<std::pair<int32_t, Chips>> VacateLeavingPlayers();

  // === Game control ===
  bool StartHand();
  bool ProcessAction(int32_t player_id, const GameAction& action);

  // Make the deal reproducible for benchmarking / tests: subsequent hands
  // derive a distinct deterministic 256-bit deck seed from `seed` (per-hand,
  // via splitmix64) instead of drawing fresh OS entropy. NOT for production —
  // reusing a public base seed defeats the commit-reveal fairness guarantee.
  void SetDeterministicDeckSeed(uint64_t seed);

  // Reset a seated player's stack in place and mark them seated (SITTING) for
  // the next hand. Benchmark/test convenience so the arena runner can reuse one
  // table across thousands of hands without RemovePlayer/AddPlayerAtSeat churn.
  // NOT for production — bypasses buy-in/wallet accounting.
  bool SetPlayerChips(int32_t player_id, Chips chips);

  // Build a redacted, imperfect-information view for `viewer_id`: public table
  // state plus ONLY the viewer's own hole cards. Opponents' hole cards are never
  // copied out (see Observation / PlayerView). This is the boundary an agent's
  // Decide() consumes so it cannot peek at hidden information. Returns an
  // Observation with viewer_id == -1's own cards empty if the id is not seated.
  Observation ObserveFor(int32_t viewer_id) const;

  // === Queries ===
  const std::vector<PlayerState>& AllPlayers() const { return players_; }
  const CommunityCards& GetCommunity() const { return community_; }
  GamePhase GetPhase() const { return phase_; }
  const PotManager& GetPotManager() const { return pot_manager_; }
  Chips GetCurrentBet() const { return current_bet_; }
  Chips GetPot() const;
  bool IsHandInProgress() const;

  // RNG fairness: proof of the shuffle used for the most recent hand.
  // Persist this with the hand record so auditors can verify the deal.
  const std::string& GetLastRngProof() const { return last_rng_proof_; }

  // Commitment (SHA256(seed‖nonce)) for the hand in progress. Bound at
  // StartHand before any card is dealt; publish to clients pre-deal so the
  // revealed (seed, nonce) at hand end is verifiable against it. Empty when
  // no hand is in progress.
  std::string GetRngCommitment() const {
    return hand_started_ ? rng_dealer_.GetProof().commitment : std::string();
  }

  // Thread safety: lock before modifying game state across threads.
  void Lock() const { state_mutex_.lock(); }
  void Unlock() const { state_mutex_.unlock(); }

  // Timeout enforcement: auto-folds the current player if they exceed
  // the per-hand action timeout. Returns true if timeout triggered.
  bool CheckTimeout();

  // Reconnect: clear disconnect grace-period timer for a returning player.
  void ReconnectPlayer(int32_t player_id);

  std::vector<PlayerState*> ActivePlayers();
  int ActivePlayerCount() const;
  int GetActivePlayerCount() const { return ActivePlayerCount(); }
  PlayerState* GetPlayerAtSeat(uint8_t seat);
  int32_t GetCurrentPlayerId() const;

  // Enumerate the legal actions for `player_id` in the current state. This is a
  // read-only helper for agents and benchmarking: every returned GameAction
  // passes ActionValidator::Validate. Returns empty when it is not this
  // player's turn. Aggressive actions (BET/RAISE) carry the *minimum* legal
  // size; callers may scale the amount up to all-in.
  std::vector<GameAction> LegalActions(int32_t player_id) const;

  using EventCallback = std::function<void(const GameEvent&)>;
  void SetCallback(EventCallback cb) { event_callback_ = cb; }

  std::string ToString() const;

 private:
  TableConfig config_;
  std::vector<PlayerState> players_;
  CommunityCards community_;
  PotManager pot_manager_;

  GamePhase phase_ = GamePhase::WAITING;
  uint8_t dealer_seat_ = 0;
  uint8_t action_seat_ = 0;
  Chips current_bet_ = 0;
  // Size of the previous FULL raise this street (NLHE min-raise tracking).
  // Reset to the big blind at the start of every betting round; a short
  // all-in raise does not lower it (no "reopen" semantics here).
  Chips last_raise_size_ = 0;
  int hand_counter_ = 0;
  bool hand_started_ = false;
  Dealer rng_dealer_;
  std::string last_rng_proof_;
  // When set, StartHand uses a deterministic per-hand deck seed derived from
  // this base value (benchmarking / tests only; see SetDeterministicDeckSeed).
  std::optional<uint64_t> deck_seed_;
  int num_active_ = 0;
  int num_all_in_ = 0;
  int num_folded_ = 0;
  int actions_since_last_bet_ = 0;

  EventCallback event_callback_;

  // Serializes all state-mutating operations (ProcessAction, StartHand).
  // Recursive to allow nested internal calls within a single logical action.
  mutable std::recursive_mutex state_mutex_;

  // Action timeout deadline. Set when a player needs to act.
  std::chrono::steady_clock::time_point action_deadline_{};
  int32_t timeout_player_id_ = -1;  // player expected to act

  // Disconnect grace periods: player_id -> disconnect timestamp.
  // If the player reconnects before the grace period expires, they're restored.
  std::unordered_map<int32_t, std::chrono::steady_clock::time_point> disconnects_;

  void RotateDealer();
  void PostBlinds();
  void DealHoleCards();
  void DealCommunity(int count);
  void DealFlop();
  void DealTurn();
  void DealRiver();
  void StartBettingRound();
  void EndBettingRound();
  void AdvanceStreet();
  void DoShowdown();
  void ResetHand();
  void ResetStreet();
  void NextActionSeat();
  void CheckCapEffect();
  bool IsBettingRoundComplete();
  void EmitEvent(GameEvent::Type type, const std::string& msg = "");
  void EmitActionEvent(int32_t player_id, ActionType action, Chips amount,
                       const std::string& msg);
  int CurrentStreet() const;

  uint8_t FindNextSeated(uint8_t from_seat) const;
  void FindActiveSeat(uint8_t from_seat);
  PlayerState* GetCurrentPlayer();
};

}  // namespace poker_engine::game
