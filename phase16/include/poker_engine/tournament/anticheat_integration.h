#pragma once

#include <functional>

#include "poker_engine/anticheat/admin_handler.h"
#include "poker_engine/anticheat/anticheat.h"
#include "poker_engine/tournament/tournament.h"

namespace poker_engine::tournament {

struct AntiCheatAction {
  int64_t player_id;
  std::string action;
  std::string reason;
  float confidence;
  int64_t timestamp;
};

class AntiCheatTournamentBridge {
 public:
  explicit AntiCheatTournamentBridge(tournament::TournamentManager& tm,
                                     anticheat::AntiCheatManager& acm);

  void OnHandComplete(const game::GameState& state);
  void PeriodicFullAnalysis();
  std::vector<anticheat::CheatAlert> GetTournamentAlerts();
  bool ProcessAdminAction(const anticheat::AdminCommand& cmd);
  std::string ExportTournamentReport(int64_t tournament_id);
  void OnCheatAlert(const std::function<void(const anticheat::CheatAlert*)>& cb);

 private:
  TournamentManager& tournament_;
  anticheat::AntiCheatManager& anticheat_;
  anticheat::CaseReviewQueue review_queue_;
  std::vector<AntiCheatAction> pending_actions_;
  std::function<void(const anticheat::CheatAlert*)> alert_callback_;

  void EvaluatePlayer(int64_t player_id);
  void ProcessPendingActions();
};

struct BanPolicy {
  float kick_threshold = 60.0f;
  float ban_threshold = 80.0f;
  float collusion_ban_threshold = 50.0f;
  int max_warnings = 2;

  bool ShouldKick(const anticheat::CheatAlert& alert) const;
  bool ShouldBan(const anticheat::CheatAlert& alert) const;
};

}  // namespace poker_engine::tournament
