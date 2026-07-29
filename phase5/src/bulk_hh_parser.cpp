#include "poker_engine/phase5/bulk_hh_parser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

namespace poker_engine {
namespace phase5 {

namespace fs = std::filesystem;

std::string ParseStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "\n=== Parse Statistics ===\n\n";
  oss << "Files:    " << total_files << "\n";
  oss << "Hands:    " << total_hands << " (" << successful << " ok, " << failed << " failed)\n";
  oss << "Speed:    " << hands_per_second << " hands/sec\n";
  oss << "Time:     " << elapsed_seconds << "s\n\n";

  if (!sites.empty()) {
    oss << "By Site:\n";
    for (const auto& [site, count] : sites)
      oss << "  " << std::setw(16) << std::left << site << ": " << count << "\n";
  }
  if (!game_types.empty()) {
    oss << "\nBy Game Type:\n";
    for (const auto& [gt, count] : game_types)
      oss << "  " << std::setw(24) << std::left << gt << ": " << count << "\n";
  }
  return oss.str();
}

BulkHandHistoryParser::BulkHandHistoryParser() {}

BulkHandHistoryParser::BulkHandHistoryParser(const BulkParseConfig& config) : config_(config) {}

std::vector<poker_engine::phase4::HandHistory> BulkHandHistoryParser::ParseFile(
    const std::string& filepath) {
  phase4::HandHistoryParser parser;
  auto sourced = parser.ParseFromFile(filepath);

  HandWithMeta hwm;
  hwm.hh = sourced.hh;
  hwm.source_file = filepath;
  hwm.parsed_ok = sourced.parsed_ok;
  hwm.error = sourced.error_msg;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.push_back(hwm);
  }

  if (sourced.parsed_ok) {
    stats_.successful++;
    stats_.total_hands++;
    stats_.sites[sourced.hh.site]++;
    stats_.game_types[sourced.hh.game_type]++;
  } else {
    stats_.failed++;
  }
  stats_.total_files++;

  if (sourced.parsed_ok) return {sourced.hh};
  return {};
}

ParseStats BulkHandHistoryParser::ParseDirectory(const std::string& dir_path) {
  auto start = std::chrono::steady_clock::now();

  auto files = ListFiles(dir_path, config_.file_extension);
  stats_.total_files = files.size();

  for (const auto& f : files) {
    ParseFileInternal(f);
  }

  auto end = std::chrono::steady_clock::now();
  stats_.elapsed_seconds = std::chrono::duration<double>(end - start).count();
  stats_.hands_per_second =
      stats_.elapsed_seconds > 0 ? stats_.total_hands / stats_.elapsed_seconds : 0;

  return stats_;
}

ParseStats BulkHandHistoryParser::ParseDirectories(const std::vector<std::string>& dir_paths) {
  auto start = std::chrono::steady_clock::now();

  for (const auto& dir : dir_paths) {
    auto files = ListFiles(dir, config_.file_extension);
    stats_.total_files += files.size();
    for (const auto& f : files) {
      ParseFileInternal(f);
    }
  }

  auto end = std::chrono::steady_clock::now();
  stats_.elapsed_seconds = std::chrono::duration<double>(end - start).count();
  stats_.hands_per_second =
      stats_.elapsed_seconds > 0 ? stats_.total_hands / stats_.elapsed_seconds : 0;

  return stats_;
}

void BulkHandHistoryParser::ParseFileInternal(const std::string& filepath) {
  phase4::HandHistoryParser parser;
  auto sourced = parser.ParseFromFile(filepath);

  HandWithMeta hwm;
  hwm.hh = sourced.hh;
  hwm.source_file = filepath;
  hwm.parsed_ok = sourced.parsed_ok;
  hwm.error = sourced.error_msg;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.push_back(hwm);
  }

  if (sourced.parsed_ok) {
    stats_.successful++;
    stats_.total_hands++;
    stats_.sites[sourced.hh.site]++;
    stats_.game_types[sourced.hh.game_type]++;
  } else {
    stats_.failed++;
  }
}

std::vector<std::string> BulkHandHistoryParser::ListFiles(const std::string& dir_path,
                                                          const std::string& extension) {
  std::vector<std::string> files;
  if (!fs::exists(dir_path)) return files;

  for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      files.push_back(entry.path().string());
    }
  }
  return files;
}

std::string BulkHandHistoryParser::ToJSON() const {
  std::ostringstream oss;
  oss << "{\"hands\": [";
  for (size_t i = 0; i < results_.size(); i++) {
    if (i > 0) oss << ",";
    oss << "{\"file\":\"" << results_[i].source_file << "\""
        << ",\"ok\":" << (results_[i].parsed_ok ? "true" : "false")
        << ",\"id\":" << results_[i].hh.hand_id << "}";
  }
  oss << "]}";
  return oss.str();
}

bool BulkHandHistoryParser::ExportJSON(const std::string& filepath) const {
  std::ofstream out(filepath);
  if (!out.is_open()) return false;
  out << ToJSON();
  out.close();
  return true;
}

// ===================== HandDatabase =====================

void HandDatabase::AddHand(const HandWithMeta& hand) {
  size_t idx = hands_.size();
  hands_.push_back(hand);
  player_index_.insert({hand.hh.HeroName(), idx});
  site_index_.insert({hand.hh.site, idx});
}

void HandDatabase::AddHands(const std::vector<HandWithMeta>& hands) {
  for (const auto& h : hands) AddHand(h);
}

void HandDatabase::Clear() {
  hands_.clear();
  player_index_.clear();
  site_index_.clear();
}

std::vector<HandWithMeta> HandDatabase::GetHandsByPlayer(const std::string& player) const {
  std::vector<HandWithMeta> result;
  auto range = player_index_.equal_range(player);
  for (auto it = range.first; it != range.second; ++it) result.push_back(hands_[it->second]);
  return result;
}

std::vector<HandWithMeta> HandDatabase::GetHandsBySite(const std::string& site) const {
  std::vector<HandWithMeta> result;
  auto range = site_index_.equal_range(site);
  for (auto it = range.first; it != range.second; ++it) result.push_back(hands_[it->second]);
  return result;
}

std::vector<HandWithMeta> HandDatabase::GetHandsByBBRange(double min_bb, double max_bb) const {
  std::vector<HandWithMeta> result;
  for (const auto& h : hands_) {
    if (h.hh.big_blind >= min_bb && h.hh.big_blind <= max_bb) result.push_back(h);
  }
  return result;
}

HandDatabase::AggregateStats HandDatabase::ComputeAggregate() const {
  AggregateStats agg;
  agg.hand_count = hands_.size();
  double total_pot = 0, total_players = 0;

  for (const auto& h : hands_) {
    total_pot += h.hh.total_pot;
    total_players += h.hh.seats.size();
    for (const auto& st : h.hh.streets) {
      for (const auto& a : st.actions) {
        std::string action_name;
        switch (a.action) {
          case phase4::ActionType::FOLD:
            action_name = "fold";
            break;
          case phase4::ActionType::CHECK:
            action_name = "check";
            break;
          case phase4::ActionType::CALL:
            action_name = "call";
            break;
          case phase4::ActionType::BET:
            action_name = "bet";
            break;
          case phase4::ActionType::RAISE:
            action_name = "raise";
            break;
          default:
            action_name = "other";
            break;
        }
        agg.action_distribution[action_name]++;
      }
    }
  }

  agg.avg_pot = agg.hand_count > 0 ? total_pot / agg.hand_count : 0;
  agg.avg_players = agg.hand_count > 0 ? total_players / agg.hand_count : 0;

  return agg;
}

}  // namespace phase5
}  // namespace poker_engine
