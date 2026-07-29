#include "poker_engine/phase14/stat_repository.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "poker_engine/phase14/hand_repository.h"
#include "poker_engine/phase14/player_repository.h"

namespace poker_engine::phase14 {

// ===================== SessionStats =====================
std::string SessionStats::ToString() const {
  std::ostringstream oss;
  oss << "Session #" << session_id << " @ " << table_name << " | Hands: " << hand_count
      << " | Buy-in: $" << int(total_buy_in) << " | Period: " << start_time << " -> " << end_time;
  return oss.str();
}

// ===================== PositionalStats =====================
std::string PositionalStats::ToString() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1);
  oss << "Seat " << seat << ": " << hands_played << " hands | " << int(win_rate * 100) << "% WR | "
      << int(vpip * 100) << "% VPIP | " << int(pfr * 100) << "% PFR | " << int(bb_per_100)
      << " BB/100 | "
      << "$" << int(net_profit) << " net";
  return oss.str();
}

// ===================== StatRepository =====================

StatRepository::StatRepository(Database& db, HandRepository& hr, PlayerRepository& pr)
    : db_(db), hand_repo_(hr), player_repo_(pr) {}

double StatRepository::SafeDivide(double a, double b) const { return b > 0.0001 ? a / b : 0; }

std::string StatRepository::GetHandCategory(const std::string& hole_cards) const {
  if (hole_cards.size() < 4) return "Unknown";

  const char* ranks = "23456789TJQKA";
  char r1 = hole_cards[0];
  char r2 = hole_cards[2];
  char s1 = hole_cards[1];
  char s2 = hole_cards[3];

  bool paired = (r1 == r2);
  bool suited = (s1 == s2);

  int rank_idx1 = 0, rank_idx2 = 0;
  for (int i = 0; i < 13; i++) {
    if (ranks[i] == r1) rank_idx1 = i;
    if (ranks[i] == r2) rank_idx2 = i;
  }

  int gap = std::abs(rank_idx1 - rank_idx2);

  if (paired) {
    if (rank_idx1 >= 10) return "Premium Pair (QQ+)";
    if (rank_idx1 >= 6) return "Medium Pair (77-TT)";
    return "Small Pair (22-66)";
  }

  if (suited) {
    if (gap <= 1) {
      if (rank_idx1 >= 9) return "Premium Suited Connector";
      return "Suited Connector";
    }
    if (gap <= 2) return "Suited One-Gapper";
    return "Suited";
  }

  if (gap <= 1) return "Broadway (offsuit)";
  return "Rag (offsuit)";
}

// ---- 汇总统计 ----

StatRepository::OverviewStats StatRepository::GetOverviewStats() {
  OverviewStats ov;

  auto rs = db_.Query("SELECT COUNT(*) FROM hands");
  if (rs.Next()) ov.total_hands = rs.GetRow().GetInt64(0);

  rs = db_.Query("SELECT COUNT(DISTINCT hand_id) FROM player_results WHERE won = 1");
  if (rs.Next()) ov.total_wins = rs.GetRow().GetInt64(0);

  rs = db_.Query("SELECT COUNT(DISTINCT session_id) FROM hands");
  if (rs.Next()) ov.total_sessions = rs.GetRow().GetInt64(0);

  ov.win_rate = SafeDivide(static_cast<double>(ov.total_wins), static_cast<double>(ov.total_hands));

  rs = db_.Query("SELECT SUM(total_buy_in), SUM(total_cash_out) FROM players");
  if (rs.Next()) {
    Row row = rs.GetRow();
    ov.total_buy_in = row.GetDouble(0);
    ov.total_cash_out = row.GetDouble(1);
  }

  ov.avg_net_per_hand =
      SafeDivide(ov.total_cash_out - ov.total_buy_in, static_cast<double>(ov.total_hands));
  ov.avg_bb_per_100 = ov.avg_net_per_hand * 100.0;
  ov.roi = ov.total_buy_in > 0
               ? SafeDivide(ov.total_cash_out - ov.total_buy_in, ov.total_buy_in) * 100
               : 0;

  return ov;
}

// ---- 按位置统计 ----

std::vector<PositionalStats> StatRepository::GetPositionalStats(int32_t player_id) {
  std::vector<PositionalStats> result;

  for (int seat = 0; seat < 9; seat++) {
    PositionalStats ps;
    ps.seat = seat;

    auto rs = db_.Query("SELECT COUNT(*) FROM player_results WHERE player_id = " +
                        std::to_string(player_id));
    if (rs.Next()) ps.hands_played = rs.GetRow().GetInt(0);

    result.push_back(ps);
  }

  return result;
}

// ---- 按对手统计 ----

std::vector<StatRepository::OpponentStat> StatRepository::GetOpponentStats(int32_t player_id) {
  std::vector<OpponentStat> result;

  std::string sql =
      "SELECT pr2.player_id, p.name, "
      "COUNT(*) as hands, "
      "SUM(CASE WHEN pr2.won = 1 THEN 1 ELSE 0 END) as wins_vs, "
      "SUM(pr2.net_profit) as net_vs "
      "FROM player_results pr1 "
      "JOIN player_results pr2 ON pr1.hand_id = pr2.hand_id "
      "LEFT JOIN players p ON pr2.player_id = p.player_id "
      "WHERE pr1.player_id = " +
      std::to_string(player_id) + " AND pr2.player_id != " + std::to_string(player_id) +
      " GROUP BY pr2.player_id "
      "ORDER BY net_vs DESC";

  auto rs = db_.Query(sql);
  while (rs.Next()) {
    Row row = rs.GetRow();
    OpponentStat os;
    os.opponent_id = row.GetInt(0);
    os.opponent_name = row.GetString(1);
    os.hands_played = row.GetInt(2);
    os.hands_won_vs = row.GetInt(3);
    os.net_vs = row.GetDouble(4);
    os.win_rate_vs = SafeDivide(os.hands_won_vs, os.hands_played);
    os.bb_per_100_vs = SafeDivide(os.net_vs, os.hands_played) * 100.0;
    result.push_back(os);
  }

  return result;
}

// ---- 按时间统计 ----

std::vector<StatRepository::TimeRangeStats> StatRepository::GetDailyStats(int32_t player_id) {
  std::vector<TimeRangeStats> result;

  std::string sql =
      "SELECT DATE(timestamp) as day, COUNT(*) as hands, "
      "SUM(net_profit) as net, COUNT(CASE WHEN won = 1 THEN 1 END) as wins "
      "FROM player_results "
      "WHERE player_id = " +
      std::to_string(player_id) +
      " GROUP BY DATE(timestamp) "
      "ORDER BY day DESC LIMIT 30";

  auto rs = db_.Query(sql);
  while (rs.Next()) {
    Row row = rs.GetRow();
    TimeRangeStats ts;
    ts.period = row.GetString(0);
    ts.hands = row.GetInt(1);
    ts.net = row.GetDouble(2);
    int wins = row.GetInt(3);
    ts.win_rate = SafeDivide(wins, ts.hands);
    ts.bb_per_100 = SafeDivide(ts.net, ts.hands) * 100.0;
    result.push_back(ts);
  }

  return result;
}

std::vector<StatRepository::TimeRangeStats> StatRepository::GetWeeklyStats(int32_t player_id) {
  std::vector<TimeRangeStats> result;

  std::string sql =
      "SELECT STRFTIME('%Y-W%W', timestamp) as week, COUNT(*) as hands, "
      "SUM(net_profit) as net, COUNT(CASE WHEN won = 1 THEN 1 END) as wins "
      "FROM player_results "
      "WHERE player_id = " +
      std::to_string(player_id) +
      " GROUP BY STRFTIME('%Y-W%W', timestamp) "
      "ORDER BY week DESC LIMIT 12";

  auto rs = db_.Query(sql);
  while (rs.Next()) {
    Row row = rs.GetRow();
    TimeRangeStats ts;
    ts.period = row.GetString(0);
    ts.hands = row.GetInt(1);
    ts.net = row.GetDouble(2);
    int wins = row.GetInt(3);
    ts.win_rate = SafeDivide(wins, ts.hands);
    ts.bb_per_100 = SafeDivide(ts.net, ts.hands) * 100.0;
    result.push_back(ts);
  }

  return result;
}

std::vector<StatRepository::TimeRangeStats> StatRepository::GetMonthlyStats(int32_t player_id) {
  std::vector<TimeRangeStats> result;

  std::string sql =
      "SELECT STRFTIME('%Y-%m', timestamp) as month, COUNT(*) as hands, "
      "SUM(net_profit) as net, COUNT(CASE WHEN won = 1 THEN 1 END) as wins "
      "FROM player_results "
      "WHERE player_id = " +
      std::to_string(player_id) +
      " GROUP BY STRFTIME('%Y-%m', timestamp) "
      "ORDER BY month DESC LIMIT 6";

  auto rs = db_.Query(sql);
  while (rs.Next()) {
    Row row = rs.GetRow();
    TimeRangeStats ts;
    ts.period = row.GetString(0);
    ts.hands = row.GetInt(1);
    ts.net = row.GetDouble(2);
    int wins = row.GetInt(3);
    ts.win_rate = SafeDivide(wins, ts.hands);
    ts.bb_per_100 = SafeDivide(ts.net, ts.hands) * 100.0;
    result.push_back(ts);
  }

  return result;
}

// ---- 分牌型统计 ----

std::vector<StatRepository::HandTypeStats> StatRepository::GetHandTypeStats(int32_t player_id) {
  std::map<std::string, std::tuple<int, int, double>> agg;

  auto rs = db_.Query(
      "SELECT pr.hand_id, pr.hole_cards, pr.net_profit, pr.won "
      "FROM player_results pr "
      "WHERE pr.player_id = " +
      std::to_string(player_id) + " AND pr.hole_cards != ''");

  while (rs.Next()) {
    Row row = rs.GetRow();
    std::string hole = row.GetString(1);
    double net = row.GetDouble(2);
    int won = row.GetInt(3);

    std::string category = GetHandCategory(hole);

    auto& entry = agg[category];
    std::get<0>(entry)++;
    if (won) std::get<1>(entry)++;
    std::get<2>(entry) += net;
  }

  std::vector<HandTypeStats> result;
  for (const auto& [cat, data] : agg) {
    HandTypeStats hts;
    hts.hand_category = cat;
    hts.hands = std::get<0>(data);
    hts.wins = std::get<1>(data);
    hts.win_rate = SafeDivide(hts.wins, hts.hands);
    hts.avg_net = SafeDivide(std::get<2>(data), hts.hands);
    result.push_back(hts);
  }

  std::sort(result.begin(), result.end(), [](auto& a, auto& b) { return a.avg_net > b.avg_net; });

  return result;
}

// ---- 方差统计 ----

StatRepository::VarianceStats StatRepository::GetVarianceStats(int32_t player_id) {
  VarianceStats vs;

  std::string sql =
      "SELECT pr.hand_id, pr.net_profit "
      "FROM player_results pr "
      "WHERE pr.player_id = " +
      std::to_string(player_id) + " ORDER BY pr.hand_id";

  auto rs = db_.Query(sql);
  std::vector<double> profits;
  double total = 0;
  double sum_sq = 0;
  int n = 0;

  while (rs.Next()) {
    double net = rs.GetRow().GetDouble(1);
    profits.push_back(net);
    total += net;
    sum_sq += net * net;
    n++;
  }

  vs.sample_size = n;

  if (n == 0) return vs;

  double mean = total / n;
  double variance = 0;
  if (n > 1) {
    variance = (sum_sq - n * mean * mean) / (n - 1);
  }
  double stddev = std::sqrt(std::max(0.0, variance));

  vs.mean_bb100 = mean;
  vs.std_dev_bb100 = stddev;

  double stderr_ = stddev / std::sqrt(static_cast<double>(n));
  double t = 1.96;
  if (n < 30) t = 2.0;
  if (n < 15) t = 2.14;
  if (n < 10) t = 2.26;
  if (n < 5) t = 2.78;

  vs.ci95_low = mean - t * stderr_;
  vs.ci95_high = mean + t * stderr_;

  // 最大回撤
  double peak = 0;
  double max_dd = 0;
  double cum = 0;
  for (double p : profits) {
    cum += p;
    if (cum > peak) peak = cum;
    double dd = peak - cum;
    if (dd > max_dd) max_dd = dd;
  }
  vs.max_drawdown = max_dd;
  vs.max_drawdown_pct = peak > 0 ? max_dd / peak * 100 : 0;

  vs.sharpe_ratio = stddev > 0 ? mean / stddev * std::sqrt(100) : 0;

  return vs;
}

// ---- 会话统计 ----

std::vector<SessionStats> StatRepository::GetSessionStats() {
  std::vector<SessionStats> result;

  auto rs = db_.Query(
      "SELECT session_id, MIN(timestamp), MAX(timestamp), "
      "COUNT(*), table_name "
      "FROM hands "
      "GROUP BY session_id "
      "ORDER BY session_id DESC LIMIT 20");

  while (rs.Next()) {
    Row row = rs.GetRow();
    SessionStats ss;
    ss.session_id = row.GetInt64(0);
    ss.start_time = row.GetString(1);
    ss.end_time = row.GetString(2);
    ss.hand_count = row.GetInt(3);
    ss.table_name = row.GetString(4);
    result.push_back(ss);
  }

  return result;
}

}  // namespace poker_engine::phase14
