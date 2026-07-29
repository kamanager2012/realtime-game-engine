#include "poker_engine/tournament/anticheat_integration.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

#include "poker_engine/base/logging.h"

namespace poker_engine::tournament {

namespace {

const char* PhaseToString(game::GamePhase phase) {
  switch (phase) {
    case game::GamePhase::WAITING:
      return "waiting";
    case game::GamePhase::DEALING:
      return "dealing";
    case game::GamePhase::PREFLOP_BETTING:
      return "preflop";
    case game::GamePhase::FLOP_DEALING:
      return "flop_dealing";
    case game::GamePhase::FLOP_BETTING:
      return "flop";
    case game::GamePhase::TURN_DEALING:
      return "turn_dealing";
    case game::GamePhase::TURN_BETTING:
      return "turn";
    case game::GamePhase::RIVER_DEALING:
      return "river_dealing";
    case game::GamePhase::RIVER_BETTING:
      return "river";
    case game::GamePhase::SHOWDOWN:
      return "showdown";
    case game::GamePhase::PAYOUT:
      return "payout";
    case game::GamePhase::HAND_COMPLETE:
      return "hand_over";
    default:
      return "unknown";
  }
}

bool PlayerIsActive(const game::PlayerState& p) {
  return p.seat_state == game::SeatState::PLAYING || p.seat_state == game::SeatState::FOLDED ||
         p.seat_state == game::SeatState::ALL_IN;
}

}  // namespace

AntiCheatTournamentBridge::AntiCheatTournamentBridge(TournamentManager& tm,
                                                     anticheat::AntiCheatManager& acm)
    : tournament_(tm), anticheat_(acm) {}

void AntiCheatTournamentBridge::OnHandComplete(const game::GameState& state) {
  replay::HandSnapshot snap;
  snap.hand_id = 0;
  snap.hand_number = 0;  // No public accessor for hand counter
  snap.phase = PhaseToString(state.GetPhase());
  snap.total_pot = static_cast<int64_t>(state.GetPot());
  snap.winners.clear();

  // Collect community cards
  const auto& community = state.GetCommunity();
  snap.community_cards.clear();
  for (uint8_t i = 0; i < community.count; ++i) {
    snap.community_cards.push_back(community.cards[i]);
  }

  // Collect player data from AllPlayers()
  for (const auto& p : state.AllPlayers()) {
    if (!PlayerIsActive(p)) continue;

    replay::HandSnapshot::PlayerSnap ps;
    ps.player_id = p.id;
    ps.seat_index = p.seat;
    ps.display_name = p.name;
    ps.chips_at_start = static_cast<int64_t>(p.chips + p.bet_info.total_invested);
    ps.chips_at_end = static_cast<int64_t>(p.chips);
    ps.net_profit =
        static_cast<int64_t>(p.chips) - static_cast<int64_t>(p.chips + p.bet_info.total_invested);
    ps.is_winner = false;  // Winners determined separately
    ps.status = p.IsFolded() ? "folded" : (p.IsAllIn() ? "all_in" : "active");

    if (p.HasCards()) {
      ps.hole_cards.push_back(p.hole_cards.card1());
      ps.hole_cards.push_back(p.hole_cards.card2());
    }

    snap.players.push_back(ps);
  }

  anticheat_.SubmitHandData(snap);
  if (snap.hand_number % 20 == 0) PeriodicFullAnalysis();
}

void AntiCheatTournamentBridge::PeriodicFullAnalysis() {
  PE_LOG_INFO("Running periodic anti-cheat analysis...");
  anticheat_.RunAnalysis();
  for (auto& alert : anticheat_.GetAlerts()) EvaluatePlayer(alert.player_id);
}

void AntiCheatTournamentBridge::EvaluatePlayer(int64_t player_id) {
  float score = anticheat_.GetPlayerSuspicionScore(player_id);
  AntiCheatAction action;
  action.player_id = player_id;
  action.confidence = score;
  action.timestamp = 0;
  BanPolicy policy;
  if (score >= policy.ban_threshold) {
    action.action = "ban";
    action.reason = "Suspicion score exceeds ban threshold";
    pending_actions_.push_back(action);
    PE_LOG_WARN("Player {} flagged for BAN (score={})", player_id, score);
  } else if (score >= policy.kick_threshold) {
    action.action = "kick";
    action.reason = "Suspicion score exceeds kick threshold";
    pending_actions_.push_back(action);
    PE_LOG_WARN("Player {} flagged for KICK (score={})", player_id, score);
  } else if (score > 0) {
    action.action = "flag";
    action.reason = "Elevated suspicion score";
    pending_actions_.push_back(action);
  }
}

std::vector<anticheat::CheatAlert> AntiCheatTournamentBridge::GetTournamentAlerts() {
  return anticheat_.GetAlerts();
}

bool AntiCheatTournamentBridge::ProcessAdminAction(const anticheat::AdminCommand& cmd) {
  switch (cmd.action) {
    case anticheat::AdminAction::ViewAlerts:
      return true;
    case anticheat::AdminAction::ViewPlayerReport:
      return anticheat_.GetPlayerStats(cmd.target_id) != nullptr;
    case anticheat::AdminAction::AcknowledgeAlert:
      return cmd.target_id >= 0 &&
             cmd.target_id < static_cast<int64_t>(anticheat_.GetAlerts().size());
    case anticheat::AdminAction::ForceKick:
      PE_LOG_INFO("Admin KICK player {}: {}", cmd.target_id, cmd.reason);
      return true;
    case anticheat::AdminAction::BanPlayer:
      PE_LOG_INFO("Admin BAN player {}: {}", cmd.target_id, cmd.reason);
      return true;
    case anticheat::AdminAction::RunAnalysis:
      PeriodicFullAnalysis();
      return true;
    default:
      return false;
  }
}

std::string AntiCheatTournamentBridge::ExportTournamentReport(int64_t tournament_id) {
  nlohmann::json report;
  report["tournament_id"] = tournament_id;
  auto alerts = GetTournamentAlerts();
  nlohmann::json alerts_json = nlohmann::json::array();
  for (auto& a : alerts) {
    alerts_json.push_back({{"player_id", a.player_id},
                           {"player_name", a.player_name},
                           {"level", static_cast<int>(a.level)},
                           {"score", a.score},
                           {"reason", a.reason}});
  }
  report["alerts"] = alerts_json;
  report["pending_cases"] = review_queue_.PendingCount();
  report["total_cases"] = review_queue_.TotalCount();
  return report.dump(2);
}

void AntiCheatTournamentBridge::OnCheatAlert(
    const std::function<void(const anticheat::CheatAlert*)>& cb) {
  alert_callback_ = cb;
}

bool BanPolicy::ShouldKick(const anticheat::CheatAlert& alert) const {
  return alert.score >= kick_threshold;
}

bool BanPolicy::ShouldBan(const anticheat::CheatAlert& alert) const {
  if (alert.score >= ban_threshold) return true;
  if (alert.reason.find("Collusion") != std::string::npos && alert.score >= collusion_ban_threshold)
    return true;
  return false;
}

}  // namespace poker_engine::tournament
