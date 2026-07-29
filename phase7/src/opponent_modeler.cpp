#include "poker_engine/phase7/opponent_modeler.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

#include "poker_engine/phase5/bulk_hh_parser.h"

namespace poker_engine {
namespace phase7 {
using namespace poker_engine::phase4;
using namespace poker_engine::range;

double OpponentStats::avg_bet_size() const {
  if (bet_sizes.empty()) return 0;
  double sum = 0;
  for (double d : bet_sizes) sum += d;
  return sum / bet_sizes.size();
}
double OpponentStats::avg_raise_size() const {
  if (raise_sizes.empty()) return 0;
  double sum = 0;
  for (double d : raise_sizes) sum += d;
  return sum / raise_sizes.size();
}
double OpponentStats::aggression_factor() const {
  int bets_raises = static_cast<int>(bet_sizes.size() + raise_sizes.size());
  int calls = vpip_count - pfr_count;
  return calls > 0 ? static_cast<double>(bets_raises) / calls : 0;
}
double OpponentStats::aggression_frequency() const {
  if (hands_seen == 0) return 0;
  int agg_actions = static_cast<int>(bet_sizes.size() + raise_sizes.size());
  return static_cast<double>(agg_actions) / hands_seen;
}

std::string OpponentStats::classify() const {
  double vp = vpip_pct(), pf = pfr_pct(), af = aggression_factor();
  if (vp < 15) return "Nit";
  if (vp < 25 && pf < 10) return "Rock";
  if (vp < 35 && pf > 20 && af > 2.0) return "TAG";
  if (vp < 35) return "Tight-Passive";
  if (vp < 50 && pf > 15 && af > 2.0) return "LAG";
  if (vp < 50) return "Loose-Passive";
  if (vp < 70 && af > 1.5) return "Maniac";
  return "Calling Station";
}

std::string OpponentStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "=== " << player_name << " (" << hands_seen << " hands) ===\n";
  oss << "VPIP: " << vpip_pct() << "% | PFR: " << pfr_pct() << "% | 3Bet: " << three_bet_pct()
      << "%\nC-Bet: " << cbet_pct() << "% | FoldCBet: " << fold_cbet_pct()
      << "% | ChkRaise: " << check_raise_pct() << "%\nAF: " << aggression_factor()
      << " | AFq: " << aggression_frequency() * 100 << "%\nType: " << classify() << "\n";
  return oss.str();
}

std::string OpponentCluster::ToString() const {
  std::ostringstream oss;
  oss << "Cluster: " << label << " | VPIP: " << vpip_min << "%-" << vpip_max << "% | Members: ";
  for (size_t i = 0; i < std::min(members.size(), size_t(5)); i++) {
    oss << members[i];
    if (i + 1 < std::min(members.size(), size_t(5))) oss << ", ";
  }
  if (members.size() > 5) oss << " +" << (members.size() - 5) << " more";
  return oss.str();
}

std::string OpponentPrediction::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "Prediction: " << player_name << " -> " << likely_type
      << " (VPIP conf: " << vpip_confidence * 100 << "%)\n";
  for (const auto& [type, prob] : type_probabilities)
    oss << "  " << type << ": " << prob * 100 << "%\n";
  return oss.str();
}

OpponentModeler::OpponentModeler() = default;

void OpponentModeler::ProcessHand(const HandHistory& hh, bool is_hero) {
  if (hh.seats.empty()) return;
  std::string hero_name = hh.HeroName();
  if (is_hero) hero_name = hh.seats[0].player_name;

  for (size_t si = 0; si < hh.streets.size(); si++) {
    const auto& sr = hh.streets[si];
    bool preflop = (si == 0);
    bool last_was_pfr = false;

    for (const auto& action : sr.actions) {
      std::string pn = action.player_name;
      if (pn == hero_name && !is_hero) continue;
      auto& st = stats_[pn];
      st.player_name = pn;
      if (!preflop) st.hands_seen++;

      if (preflop) {
        if (pn == hero_name) st.hands_dealt++;
        switch (action.action) {
          case ActionType::CALL:
            st.vpip_count++;
            break;
          case ActionType::BET:
            st.vpip_count++;
            st.pfr_count++;
            if (last_was_pfr)
              st.four_bet_count++;
            else
              st.three_bet_count++;
            last_was_pfr = true;
            break;
          case ActionType::RAISE:
            st.vpip_count++;
            if (st.pfr_count < st.hands_dealt) {
              st.pfr_count++;
              if (last_was_pfr)
                st.four_bet_count++;
              else
                st.three_bet_count++;
              last_was_pfr = true;
            }
            if (action.amount > 0) st.raise_sizes.push_back(action.amount);
            break;
          case ActionType::ALL_IN:
            st.vpip_count++;
            st.pfr_count++;
            st.four_bet_count++;
            break;
          default:
            break;
        }
      } else {
        if (action.action == ActionType::BET || action.action == ActionType::RAISE) {
          st.bet_sizes.push_back(action.amount);
          if (action.action == ActionType::RAISE) st.raise_sizes.push_back(action.amount);
        }
        if (action.action == ActionType::RAISE) st.check_raise_count++;
        if (si == 1 && action.action == ActionType::BET) {
          bool preflop_raiser = false;
          for (const auto& pa : hh.streets[0].actions) {
            if (pa.player_name == pn &&
                (pa.action == ActionType::BET || pa.action == ActionType::RAISE))
              preflop_raiser = true;
          }
          if (preflop_raiser || pn == hero_name) st.c_bet_count++;
        }
      }
    }
    if (si == 1 && !sr.community_cards.empty()) {
      for (auto& [name, st] : stats_) {
        if (name != hero_name) st.c_bet_possible++;
      }
    }
  }
  total_hands_++;
}

void OpponentModeler::ProcessHands(const std::vector<HandHistory>& hands) {
  for (const auto& hh : hands) ProcessHand(hh);
}

void OpponentModeler::ProcessDirectory(const std::string& dir_path) {
  poker_engine::phase5::BulkHandHistoryParser parser;
  parser.ParseDirectory(dir_path);
  for (const auto& meta : parser.GetResults()) {
    if (meta.parsed_ok) ProcessHand(meta.hh);
  }
}

OpponentStats OpponentModeler::GetStats(const std::string& player_name) const {
  auto it = stats_.find(player_name);
  return it != stats_.end() ? it->second : OpponentStats{player_name};
}

OpponentPrediction OpponentModeler::BuildPrediction(const OpponentStats& st) const {
  OpponentPrediction pred;
  pred.player_name = st.player_name;
  double n = static_cast<double>(st.hands_seen);
  double confidence = std::min(1.0, n / 100.0);
  pred.vpip_confidence = confidence;
  pred.pfr_confidence = confidence;
  pred.cbet_confidence = confidence;
  pred.likely_type = st.classify();

  std::map<std::string, double> type_scores;
  double vp = st.vpip_pct(), pf = st.pfr_pct();
  if (vp < 15)
    type_scores["Nit"] = 0.9;
  else if (vp < 25)
    type_scores["Rock"] = 0.7;
  if (vp < 35 && pf > 20) type_scores["TAG"] = 0.8;
  if (vp < 35) type_scores["Tight-Passive"] = 0.6;
  if (vp > 35 && pf < 15) type_scores["Loose-Passive"] = 0.7;
  if (vp > 50 && pf > 15) type_scores["LAG"] = 0.8;
  if (vp > 60) type_scores["Maniac"] = 0.5;

  double total_score = 0;
  for (auto& [t, s] : type_scores) total_score += s;
  if (total_score > 0)
    for (auto& [t, s] : type_scores) s /= total_score;

  pred.type_probabilities.assign(type_scores.begin(), type_scores.end());
  std::sort(pred.type_probabilities.begin(), pred.type_probabilities.end(),
            [](auto& a, auto& b) { return a.second > b.second; });
  return pred;
}

OpponentPrediction OpponentModeler::PredictStyle(const std::string& player_name) const {
  auto it = stats_.find(player_name);
  if (it == stats_.end()) {
    OpponentPrediction u;
    u.player_name = player_name;
    u.likely_type = "Unknown (no data)";
    return u;
  }
  return BuildPrediction(it->second);
}

Range OpponentModeler::EstimateRange(const std::string& player, int street,
                                     const std::string& last_action) const {
  auto it = stats_.find(player);
  double vpip = it != stats_.end() ? it->second.vpip_pct() : 30;
  std::string range_str;
  if (vpip < 15)
    range_str = "TT+,AKs,AKo";
  else if (vpip < 25)
    range_str = "77+,A9s+,K9s+,Q9s+,J9s+,AJo+,KQo";
  else if (vpip < 35)
    range_str = "22+,A2s+,K2s+,Q2s+,J2s+";
  else if (vpip < 50)
    range_str = "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,A2o+,K2o+";
  else
    range_str = "22+,A2s+,K2s+,Q2s+,J2s+,T2s+,92s+,82s+,72s+,A2o+,K2o+,Q2o+,J2o+,T2o+,92o+";
  return Range::FromString(range_str);
}

std::string OpponentModeler::PlayerReport(const std::string& player_name) const {
  return GetStats(player_name).ToString() + "\n" + PredictStyle(player_name).ToString();
}

std::string OpponentModeler::SessionReport() const {
  std::ostringstream oss;
  oss << "=== Session Opponent Report ===\nHands: " << total_hands_
      << " | Players: " << stats_.size() << "\n\n";
  for (const auto& [name, st] : stats_) {
    if (st.hands_seen >= 3)
      oss << name << ": VPIP " << st.vpip_pct() << "% | PFR " << st.pfr_pct() << "% | "
          << st.classify() << "\n";
  }
  return oss.str();
}

std::string OpponentModeler::FullReport() const {
  std::ostringstream oss;
  oss << "=== Opponent Modeling Full Report ===\n\n";
  for (const auto& [name, st] : stats_) oss << PlayerReport(name) << "\n";
  auto clusters = ClusterPlayers(4);
  oss << "\n=== Player Clusters ===\n";
  for (const auto& c : clusters) oss << c.ToString() << "\n";
  return oss.str();
}

std::vector<std::vector<double>> OpponentModeler::FeatureVectors() const {
  std::vector<std::vector<double>> features;
  for (const auto& [name, st] : stats_) {
    if (st.hands_seen < 5) continue;
    features.push_back({st.vpip_pct(), st.pfr_pct(), st.three_bet_pct(), st.cbet_pct(),
                        st.aggression_factor(), st.fold_cbet_pct()});
  }
  return features;
}

std::vector<int> OpponentModeler::KMeans(int k,
                                         const std::vector<std::vector<double>>& points) const {
  if (points.empty()) return {};
  int dims = static_cast<int>(points[0].size());
  int n = static_cast<int>(points.size());
  std::mt19937 rng(42);
  std::vector<int> labels(n);
  std::vector<std::vector<double>> centroids(k, std::vector<double>(dims, 0));
  for (int ki = 0; ki < k; ki++) {
    int idx = rng() % n;
    for (int d = 0; d < dims; d++) centroids[ki][d] = points[idx][d];
  }
  for (int iter = 0; iter < 50; iter++) {
    for (int i = 0; i < n; i++) {
      double best_dist = std::numeric_limits<double>::max();
      int best_k = 0;
      for (int ki = 0; ki < k; ki++) {
        double dist = 0;
        for (int d = 0; d < dims; d++) {
          double diff = points[i][d] - centroids[ki][d];
          dist += diff * diff;
        }
        if (dist < best_dist) {
          best_dist = dist;
          best_k = ki;
        }
      }
      labels[i] = best_k;
    }
    std::vector<std::vector<double>> nc(k, std::vector<double>(dims, 0));
    std::vector<int> counts(k, 0);
    for (int i = 0; i < n; i++) {
      for (int d = 0; d < dims; d++) nc[labels[i]][d] += points[i][d];
      counts[labels[i]]++;
    }
    for (int ki = 0; ki < k; ki++) {
      if (counts[ki] > 0)
        for (int d = 0; d < dims; d++) nc[ki][d] /= counts[ki];
    }
    centroids = nc;
  }
  return labels;
}

std::vector<OpponentCluster> OpponentModeler::ClusterPlayers(int num_clusters) const {
  auto features = FeatureVectors();
  auto labels = KMeans(num_clusters, features);
  std::vector<std::string> names;
  for (const auto& [name, st] : stats_) {
    if (st.hands_seen >= 5) names.push_back(name);
  }

  std::vector<OpponentCluster> clusters(num_clusters);
  std::vector<std::vector<double>> cluster_avgs(num_clusters, std::vector<double>(6, 0));
  std::vector<int> cluster_counts(num_clusters, 0);

  for (size_t i = 0; i < names.size() && i < labels.size(); i++) {
    int cl = labels[i];
    auto& st = stats_.at(names[i]);
    cluster_avgs[cl][0] += st.vpip_pct();
    cluster_avgs[cl][1] += st.pfr_pct();
    cluster_avgs[cl][2] += st.aggression_factor();
    cluster_avgs[cl][3] += st.cbet_pct();
    cluster_avgs[cl][4] += st.three_bet_pct();
    cluster_avgs[cl][5] += st.fold_cbet_pct();
    cluster_counts[cl]++;
    clusters[cl].members.push_back(names[i]);
  }

  for (int ki = 0; ki < num_clusters; ki++) {
    if (cluster_counts[ki] > 0) {
      for (int d = 0; d < 6; d++) cluster_avgs[ki][d] /= cluster_counts[ki];
      double vp = cluster_avgs[ki][0], pf = cluster_avgs[ki][1], af = cluster_avgs[ki][2];
      if (vp < 20)
        clusters[ki].label = "Rock";
      else if (vp < 35 && pf > 20 && af > 2)
        clusters[ki].label = "TAG";
      else if (vp > 45)
        clusters[ki].label = "LAG";
      else if (af < 1.0)
        clusters[ki].label = "Passive";
      else
        clusters[ki].label = "Mixed";
      clusters[ki].vpip_min = cluster_avgs[ki][0] - 3;
      clusters[ki].vpip_max = cluster_avgs[ki][0] + 3;
      clusters[ki].pfr_min = cluster_avgs[ki][1] - 2;
      clusters[ki].pfr_max = cluster_avgs[ki][1] + 2;
      clusters[ki].agg_min = std::max(0.0, cluster_avgs[ki][2] - 0.5);
      clusters[ki].agg_max = cluster_avgs[ki][2] + 0.5;
    } else
      clusters[ki].label = "Empty";
  }
  return clusters;
}

}  // namespace phase7
}  // namespace poker_engine
