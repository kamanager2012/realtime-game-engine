#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/cfr/cfr_training.h"
#include "tournament.h"

namespace poker_engine::tournament {

struct TournamentLeaveResult {
  bool success = false;
  std::string error;
  int64_t refund_amount = 0;
};

class TournamentServer {
 public:
  explicit TournamentServer(int port = 8080);
  ~TournamentServer();

  TournamentServer(const TournamentServer&) = delete;
  TournamentServer& operator=(const TournamentServer&) = delete;

  void Start();
  void Stop();
  bool IsRunning() const { return running_; }

  int CreateTournament(const TournamentConfig& config);
  bool JoinTournament(int tournament_id, int player_id, const std::string& name);
  bool Rebuy(int tournament_id, int player_id);

  // buy_in + entry_fee for wallet debit on registration
  std::optional<double> GetTournamentEntryCost(int tournament_id) const;
  std::optional<double> GetTournamentRebuyCost(int tournament_id) const;

  TournamentLeaveResult LeaveTournament(int tournament_id, int player_id);

  std::string GetTournamentStateJSON(int tournament_id) const;
  std::string GetLeaderboardJSON(int tournament_id) const;

  std::vector<int> ListTournaments() const;
  std::string ListTournamentsJSON() const;

  bool StartTraining(int tournament_id, const cfr::CFROptions& options = cfr::CFROptions());
  bool StopTraining(int tournament_id);

  bool SaveModel(int tournament_id, const std::string& filepath);
  bool LoadModel(int tournament_id, const std::string& filepath);

  using MessageHandler = std::function<void(int player_id, const std::string& message)>;
  void SetMessageHandler(MessageHandler handler);

  using EventCallback = std::function<void(int tournament_id, const TournamentEvent&)>;
  void SetEventCallback(EventCallback callback);

  struct ServerStats {
    int active_tournaments = 0;
    int total_players = 0;
    int active_connections = 0;
    int port = 0;
    double uptime_seconds = 0.0;
    std::string ToString() const;
  };
  ServerStats GetStats() const;

 private:
  void BroadcastEvent(int tournament_id, const TournamentEvent& event);
  void ProcessMessage(int player_id, const std::string& message);

  int port_;
  bool running_ = false;

  std::unordered_map<int, std::unique_ptr<TournamentManager>> tournaments_;
  std::unordered_map<int, std::unique_ptr<cfr::CFRTrainer>> trainers_;
  std::unordered_map<int, int> player_tournament_map_;

  int next_tournament_id_ = 1;

  MessageHandler message_handler_;
  EventCallback event_callback_;

  mutable std::mutex mutex_;
  std::mt19937 rng_{42};
};

}  // namespace poker_engine::tournament
