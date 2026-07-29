#include "poker_engine/tournament/tournament_server.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

#include "poker_engine/base/logging.h"

namespace poker_engine::tournament {

TournamentServer::TournamentServer(int port) : port_(port) {}

TournamentServer::~TournamentServer() { Stop(); }

int TournamentServer::CreateTournament(const TournamentConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);

  int tournament_id = next_tournament_id_++;
  tournaments_[tournament_id] = std::make_unique<TournamentManager>(config);
  trainers_[tournament_id] = std::make_unique<cfr::CFRTrainer>();

  PE_LOG_INFO("Created tournament #{}: {}", tournament_id, config.name);
  return tournament_id;
}


std::optional<double> TournamentServer::GetTournamentEntryCost(int tournament_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) return std::nullopt;
  const auto& cfg = it->second->Config();
  return cfg.buy_in + cfg.entry_fee;
}


std::optional<double> TournamentServer::GetTournamentRebuyCost(int tournament_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) return std::nullopt;
  const auto& cfg = it->second->Config();
  if (!cfg.has_rebuys || cfg.rebuy_cost <= 0) return std::nullopt;
  return cfg.rebuy_cost;
}

TournamentLeaveResult TournamentServer::LeaveTournament(int tournament_id, int player_id) {
  TournamentLeaveResult result;
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) {
    result.error = "tournament_not_found";
    return result;
  }

  auto map_it = player_tournament_map_.find(player_id);
  if (map_it == player_tournament_map_.end() || map_it->second != tournament_id) {
    result.error = "not_registered";
    return result;
  }

  if (it->second->Status() == TournamentStatus::Registration) {
    const auto& cfg = it->second->Config();
    result.refund_amount = static_cast<int64_t>(cfg.buy_in + cfg.entry_fee);
  }

  if (!it->second->UnregisterPlayer(player_id)) {
    result.error = "leave_failed";
    return result;
  }

  player_tournament_map_.erase(player_id);
  result.success = true;
  PE_LOG_INFO("Player {} left tournament {}", player_id, tournament_id);
  return result;
}

bool TournamentServer::JoinTournament(int tournament_id, int player_id, const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) {
    PE_LOG_WARN("Tournament {} not found", tournament_id);
    return false;
  }

  if (player_tournament_map_.count(player_id) > 0) {
    PE_LOG_WARN("Player {} already in tournament {}", player_id, player_tournament_map_[player_id]);
    return false;
  }

  bool joined = it->second->RegisterPlayer(player_id, name);
  if (joined) {
    player_tournament_map_[player_id] = tournament_id;
    PE_LOG_INFO("Player {} ({}) joined tournament {}", player_id, name, tournament_id);
  }

  return joined;
}

bool TournamentServer::Rebuy(int tournament_id, int player_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) return false;

  return it->second->ProcessRebuy(player_id);
}

std::string TournamentServer::GetTournamentStateJSON(int tournament_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) return "{}";

  auto& tm = it->second;
  std::ostringstream oss;
  oss << "{";
  oss << "\"id\":" << tournament_id << ",";
  oss << "\"status\":\"" << TournamentStatusName(tm->Status()) << "\",";
  oss << "\"players\":" << tm->ActivePlayerCount() << ",";
  oss << "\"tables\":" << tm->TableCount() << ",";
  oss << "\"blinds\":\"" << tm->CurrentSmallBlind() << "/" << tm->CurrentBigBlind() << "\",";
  oss << "\"prize_pool\":" << tm->PrizePool() << ",";
  oss << "\"level\":" << tm->CurrentBlindLevel();
  oss << "}";

  return oss.str();
}

std::string TournamentServer::GetLeaderboardJSON(int tournament_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tournaments_.find(tournament_id);
  if (it == tournaments_.end()) return "[]";

  auto result = it->second->GetResult();
  std::ostringstream oss;
  oss << "[";

  for (size_t i = 0; i < result.final_standings.size(); ++i) {
    if (i > 0) oss << ",";
    auto& p = result.final_standings[i];
    oss << "{";
    oss << "\"id\":" << p.id << ",";
    oss << "\"name\":\"" << p.name << "\",";
    oss << "\"chips\":" << p.chips << ",";
    oss << "\"eliminated\":" << (p.eliminated ? "true" : "false");
    oss << "}";
  }

  oss << "]";
  return oss.str();
}

std::vector<int> TournamentServer::ListTournaments() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<int> ids;
  for (auto& [id, _] : tournaments_) {
    ids.push_back(id);
  }
  return ids;
}

std::string TournamentServer::ListTournamentsJSON() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostringstream oss;
  oss << "[";
  bool first = true;
  for (auto& [id, tm] : tournaments_) {
    if (!first) oss << ",";
    first = false;
    oss << "{";
    oss << "\"id\":" << id << ",";
    oss << "\"name\":\"" << tm->Config().name << "\",";
    oss << "\"status\":\"" << TournamentStatusName(tm->Status()) << "\",";
    oss << "\"players\":" << tm->ActivePlayerCount();
    oss << "}";
  }
  oss << "]";
  return oss.str();
}

bool TournamentServer::StartTraining(int tournament_id, const cfr::CFROptions& options) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = trainers_.find(tournament_id);
  if (it == trainers_.end()) return false;

  it->second->SetIterations(options.config.num_iterations);
  it->second->Train(options.config.num_iterations);

  PE_LOG_INFO("Training started for tournament {}", tournament_id);
  return true;
}

bool TournamentServer::StopTraining(int tournament_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = trainers_.find(tournament_id);
  if (it == trainers_.end()) return false;

  PE_LOG_INFO("Training stopped for tournament {}", tournament_id);
  return true;
}

bool TournamentServer::SaveModel(int tournament_id, const std::string& filepath) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = trainers_.find(tournament_id);
  if (it == trainers_.end()) return false;

  return it->second->SaveModel(filepath);
}

bool TournamentServer::LoadModel(int tournament_id, const std::string& filepath) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = trainers_.find(tournament_id);
  if (it == trainers_.end()) return false;

  return it->second->LoadModel(filepath);
}

void TournamentServer::SetMessageHandler(MessageHandler handler) {
  message_handler_ = std::move(handler);
}

void TournamentServer::SetEventCallback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

void TournamentServer::Start() {
  if (running_) return;
  running_ = true;
  PE_LOG_INFO("TournamentServer started on port {}", port_);
}

void TournamentServer::Stop() {
  if (!running_) return;
  running_ = false;
  PE_LOG_INFO("TournamentServer stopped");
}

void TournamentServer::BroadcastEvent(int tournament_id, const TournamentEvent& event) {
  if (event_callback_) {
    event_callback_(tournament_id, event);
  }
}

void TournamentServer::ProcessMessage(int player_id, const std::string& message) {
  if (message_handler_) {
    message_handler_(player_id, message);
  }
}

TournamentServer::ServerStats TournamentServer::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);

  ServerStats stats;
  stats.active_tournaments = static_cast<int>(tournaments_.size());
  stats.total_players = static_cast<int>(player_tournament_map_.size());
  stats.active_connections = 0;
  stats.port = port_;

  return stats;
}

std::string TournamentServer::ServerStats::ToString() const {
  std::ostringstream oss;
  oss << "ServerStats(tournaments=" << active_tournaments << ", players=" << total_players
      << ", port=" << port << ")";
  return oss.str();
}

}  // namespace poker_engine::tournament
