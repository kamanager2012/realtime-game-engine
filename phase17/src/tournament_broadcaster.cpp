#include "poker_engine/spectator/tournament_broadcaster.h"

#include "poker_engine/base/logging.h"

namespace poker_engine::spectator {

// ==================== TournamentBroadcaster ====================

TournamentBroadcaster::TournamentBroadcaster(uint64_t tournament_id, SpectatorManager& spec_mgr)
    : Actor(tournament_id), tournament_id_(tournament_id), spec_mgr_(spec_mgr) {
  RegisterHandler("hand_event", [this](const concurrency::MessageEnvelope& msg) {
    try {
      auto j = nlohmann::json::parse(msg.payload);
      game::GameAction action;
      action.player_id = j.value("player_id", -1);
      action.amount = j.value("amount", 0);
      std::string atype = j.value("action", "fold");
      if (atype == "fold")
        action.type = game::ActionType::FOLD;
      else if (atype == "call")
        action.type = game::ActionType::CALL;
      else if (atype == "bet")
        action.type = game::ActionType::BET;
      else if (atype == "raise")
        action.type = game::ActionType::RAISE;
      else if (atype == "all_in")
        action.type = game::ActionType::ALL_IN;
      else
        action.type = game::ActionType::CHECK;
      action.street = static_cast<int16_t>(j.value("street", 0));

      OnHandEvent(msg.sender_id, action);
    } catch (...) {
    }
  });

  RegisterHandler("hand_complete", [this](const concurrency::MessageEnvelope& msg) {
    // GameState cannot be deserialized from JSON, so broadcast the
    // raw payload as a HandState event and then trigger a leaderboard update.
    nlohmann::json payload;
    try {
      payload = nlohmann::json::parse(msg.payload);
    } catch (...) {
      return;
    }
    BroadcastEvent(SpectatorMessageType::HandState, payload);
    spec_mgr_.BroadcastLeaderboard(tournament_id_);
  });

  RegisterHandler("tournament_event", [this](const concurrency::MessageEnvelope& msg) {
    try {
      auto j = nlohmann::json::parse(msg.payload);
      tournament::TournamentEvent event;
      event.type = static_cast<tournament::TournamentEvent::Type>(j.value("event_type", 0));
      event.detail = j.value("details", "");
      OnTournamentEvent(event);
    } catch (...) {
    }
  });
}

void TournamentBroadcaster::OnHandEvent(uint64_t hand_id, const game::GameAction& action) {
  spec_mgr_.OnHandEvent(tournament_id_, hand_id, action);
}

void TournamentBroadcaster::OnHandComplete(uint64_t hand_id, const game::GameState& state) {
  spec_mgr_.OnHandComplete(tournament_id_, hand_id, state);
}

void TournamentBroadcaster::OnTournamentEvent(const tournament::TournamentEvent& event) {
  spec_mgr_.OnTournamentEvent(tournament_id_, event);
}

void TournamentBroadcaster::OnLeaderboardUpdate() {
  spec_mgr_.BroadcastLeaderboard(tournament_id_);
}

void TournamentBroadcaster::OnReceive(const concurrency::MessageEnvelope& msg) {
  // Handled by registered handlers; no default action needed
  (void)msg;
}

void TournamentBroadcaster::BroadcastEvent(SpectatorMessageType type,
                                           const nlohmann::json& payload) {
  SpectatorEvent evt;
  evt.type = type;
  evt.tournament_id = tournament_id_;
  evt.sequence_id = ++event_counter_;
  evt.payload = payload.dump();

  spec_mgr_.BroadcastEvent(tournament_id_, evt);
}

// ==================== BroadcasterFactory ====================

BroadcasterFactory::BroadcasterFactory(SpectatorManager& spec_mgr) : spec_mgr_(spec_mgr) {}

TournamentBroadcaster* BroadcasterFactory::GetOrCreate(uint64_t tournament_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = broadcasters_.find(tournament_id);
  if (it != broadcasters_.end()) return it->second.get();

  auto bc = std::make_unique<TournamentBroadcaster>(tournament_id, spec_mgr_);
  bc->Start();
  auto* ptr = bc.get();
  broadcasters_[tournament_id] = std::move(bc);
  return ptr;
}

void BroadcasterFactory::Remove(uint64_t tournament_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  broadcasters_.erase(tournament_id);
}

}  // namespace poker_engine::spectator
