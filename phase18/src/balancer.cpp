#include "poker_engine/tournament/balancer.h"

#include <stdexcept>

#include "poker_engine/base/logging.h"

namespace poker_engine::tournament {

// ==================== SwissSystem ====================

std::vector<SwissPairing> SwissSystem::GeneratePairings(
    const std::vector<TournamentPlayer>& players,
    const std::vector<std::pair<int64_t, int64_t>>& previous_rounds) {
  std::vector<SwissPairing> pairings;

  // Step 1: 按分数分组
  auto groups = ScoreGroups(players);

  // Step 2: 每组内配对
  for (auto& group : groups) {
    if (group.size() < 2) {
      // 奇数个玩家：将最后一名升到下一组
      if (group.size() == 1 && !groups.empty()) {
        // 简化：标记为 BYE
        PE_LOG_INFO("Player {} gets a BYE", group[0]);
        continue;
      }
      continue;
    }

    // 组内排序 (按分数降序)
    std::sort(group.begin(), group.end(),
              [&players](int64_t a, int64_t b) { return players[a].chips > players[b].chips; });

    // 构建已配对集合
    std::unordered_map<int64_t, int64_t> paired;
    for (auto& [a, b] : previous_rounds) {
      paired[a] = b;
      paired[b] = a;
    }

    // 从上到下配对
    std::vector<bool> used(group.size(), false);

    for (size_t i = 0; i < group.size(); ++i) {
      if (used[i]) continue;

      int64_t player_i = group[i];
      int best_j = -1;
      double best_quality = -1.0;

      for (size_t j = i + 1; j < group.size(); ++j) {
        if (used[j]) continue;

        int64_t player_j = group[j];

        // 检查是否已配对过
        if (config_.avoid_repeat_pairs) {
          auto it = paired.find(player_i);
          if (it != paired.end() && it->second == player_j) continue;
        }

        // 计算配对质量
        double quality =
            PairingQuality(player_i, player_j, players, players[player_i], players[player_j]);

        if (quality > best_quality) {
          best_quality = quality;
          best_j = static_cast<int>(j);
        }
      }

      if (best_j >= 0) {
        int64_t player_j = group[static_cast<size_t>(best_j)];

        SwissPairing pair;
        pair.player_a = player_i;
        pair.player_b = player_j;
        pair.match_quality = best_quality;
        pair.seat_a = 0;  // 由 SeatAllocator 填充
        pair.seat_b = 1;

        pairings.push_back(std::move(pair));
        used[i] = true;
        used[static_cast<size_t>(best_j)] = true;
      }
    }
  }

  return pairings;
}

std::vector<std::pair<int64_t, float>> SwissSystem::ComputeTiebreaks(
    const std::vector<TournamentPlayer>& players,
    const std::vector<std::vector<std::pair<int64_t, int64_t>>>& round_history) {
  // 简化实现
  return {};
}

double SwissSystem::PairingQuality(int64_t id_a, int64_t id_b,
                                   const std::vector<TournamentPlayer>& /*players*/,
                                   const TournamentPlayer& pa, const TournamentPlayer& pb) {
  // 配对质量基于:
  // 1. 筹码接近度 (希望相近水平的玩家对战)
  // 2. 避免重复配对
  // 3. 平衡颜色 (扑克中忽略)

  double chip_diff = std::abs(static_cast<double>(pa.chips - pb.chips));
  double max_chips =
      std::max(std::abs(static_cast<double>(pa.chips)), std::abs(static_cast<double>(pb.chips)));

  // 分数差越小，配对质量越高
  double chip_ratio = (max_chips > 0) ? 1.0 - (chip_diff / max_chips) : 1.0;

  // 历史配对惩罚
  double history_penalty = 1.0;

  // 理想分差
  double ideal_diff = config_.ideal_pairing_score_diff;
  double diff = std::abs(pa.chips - pb.chips);
  double score = std::exp(-diff * diff / (2.0 * ideal_diff * ideal_diff + 1e-10));

  return score * 0.7 + chip_ratio * 0.3;
}

std::vector<std::vector<int64_t>> SwissSystem::ScoreGroups(
    const std::vector<TournamentPlayer>& players) {
  // 按筹码分组 (简化: 按筹码范围分组)
  std::vector<std::vector<int64_t>> groups;

  // 找到筹码范围
  double min_chips = std::numeric_limits<double>::max();
  double max_chips = std::numeric_limits<double>::lowest();
  for (const auto& p : players) {
    if (p.active) {
      min_chips = std::min(min_chips, p.chips);
      max_chips = std::max(max_chips, p.chips);
    }
  }

  // 分成若干组
  int num_groups = std::max(1, static_cast<int>(players.size()) / 4);
  double range = max_chips - min_chips;
  if (range < 1e-6) range = 1.0;

  groups.resize(num_groups);
  for (size_t i = 0; i < players.size(); ++i) {
    if (!players[i].active) continue;
    int group_idx = std::min(num_groups - 1,
                             static_cast<int>((players[i].chips - min_chips) / range * num_groups));
    groups[group_idx].push_back(static_cast<int64_t>(i));
  }

  // 合并空组
  std::vector<std::vector<int64_t>> non_empty;
  for (auto& g : groups) {
    if (!g.empty()) non_empty.push_back(std::move(g));
  }

  return non_empty;
}

std::vector<SwissPairing> SwissSystem::PairWithinGroup(
    const std::vector<int64_t>& group, const std::vector<TournamentPlayer>& players,
    const std::vector<std::pair<int64_t, int64_t>>& previous_pairings) {
  // 委托给 GeneratePairings 的内部逻辑
  return {};
}

// ==================== SeatAllocator ====================

std::vector<int> SeatAllocator::OptimalAssignment(int num_seats,
                                                  const std::vector<SeatPreference>& preferences) {
  int n = preferences.size();
  int seats = num_seats;

  if (n == 0 || seats == 0) return {};

  // 构建成本矩阵
  std::vector<std::vector<double>> cost(n, std::vector<double>(seats));
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < seats; ++j) {
      double c = 0.0;
      if (preferences[i].preferred_seat >= 0) {
        c = std::abs(j - preferences[i].preferred_seat);
      }
      if (preferences[i].avoid_window && (j == 0 || j == seats - 1)) {
        c += 5.0;
      }
      if (preferences[i].prefer_center) {
        c += std::abs(j - seats / 2) * 0.5;
      }
      cost[i][j] = c;
    }
  }

  // 问题规模小时用匈牙利算法
  if (n <= 20 && seats <= 20) {
    return HungarianAlgorithm(cost);
  }

  // 否则用贪心
  return GreedyAssignment(num_seats, preferences);
}

std::vector<int> SeatAllocator::GreedyAssignment(int num_seats,
                                                 const std::vector<SeatPreference>& preferences) {
  std::vector<int> assignment(preferences.size(), -1);
  std::vector<bool> seat_used(num_seats, false);

  // 按优先级排序 (有偏好的先分配)
  std::vector<int> order(preferences.size());
  std::iota(order.begin(), order.end(), 0);

  std::sort(order.begin(), order.end(), [&preferences](int a, int b) {
    return preferences[a].preferred_seat >= 0 && preferences[b].preferred_seat < 0;
  });

  for (int idx : order) {
    const auto& pref = preferences[idx];

    if (pref.preferred_seat >= 0 && pref.preferred_seat < num_seats &&
        !seat_used[pref.preferred_seat]) {
      assignment[idx] = pref.preferred_seat;
      seat_used[pref.preferred_seat] = true;
    } else {
      // 找最优可用座位
      int best_seat = -1;
      double best_cost = std::numeric_limits<double>::max();

      for (int s = 0; s < num_seats; ++s) {
        if (seat_used[s]) continue;

        double cost = 0.0;
        if (pref.preferred_seat >= 0) {
          cost += std::abs(s - pref.preferred_seat) * 2.0;
        }
        if (pref.avoid_window && (s == 0 || s == num_seats - 1)) {
          cost += 5.0;
        }
        if (pref.prefer_center) {
          cost += std::abs(s - num_seats / 2) * 0.5;
        }

        if (cost < best_cost) {
          best_cost = cost;
          best_seat = s;
        }
      }

      if (best_seat >= 0) {
        assignment[idx] = best_seat;
        seat_used[best_seat] = true;
      }
    }
  }

  return assignment;
}

std::vector<int> SeatAllocator::HungarianAlgorithm(
    const std::vector<std::vector<double>>& cost_matrix) {
  int n = cost_matrix.size();
  int m = cost_matrix[0].size();
  int sz = std::max(n, m);

  // 使用 Jonker-Volgenant 算法的简化实现
  // 实际项目中应使用成熟库 (如 LAPACK 或专门的 Hungarian 库)

  std::vector<int> assignment(n, -1);
  std::vector<bool> used_col(m, false);
  std::vector<bool> used_row(n, false);

  // 贪心初始解
  for (int i = 0; i < n; ++i) {
    int best_j = -1;
    double best_cost = std::numeric_limits<double>::max();

    for (int j = 0; j < m; ++j) {
      if (!used_col[j] && cost_matrix[i][j] < best_cost) {
        best_cost = cost_matrix[i][j];
        best_j = j;
      }
    }

    if (best_j >= 0) {
      assignment[i] = best_j;
      used_col[best_j] = true;
    }
  }

  // 简化的优化迭代 (通常 Hungarian 需要更复杂的增广路径算法)
  // 这里只做 5 轮 2-opt 改进
  for (int iter = 0; iter < 5; ++iter) {
    bool improved = false;
    for (int i1 = 0; i1 < n; ++i1) {
      for (int i2 = i1 + 1; i2 < n; ++i2) {
        if (assignment[i1] >= 0 && assignment[i2] >= 0) {
          double old_cost = cost_matrix[i1][assignment[i1]] + cost_matrix[i2][assignment[i2]];
          double new_cost = cost_matrix[i1][assignment[i2]] + cost_matrix[i2][assignment[i1]];

          if (new_cost < old_cost - 1e-9) {
            std::swap(assignment[i1], assignment[i2]);
            improved = true;
          }
        }
      }
    }
    if (!improved) break;
  }

  return assignment;
}

// ==================== SubstitutionManager ====================

bool SubstitutionManager::RequestSubstitution(int64_t original_id, int64_t substitute_id) {
  SubstitutionRequest req;
  req.original_player_id = original_id;
  req.substitute_player_id = substitute_id;
  req.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  req.approved = false;

  history_.push_back(req);
  return true;
}

std::optional<int64_t> SubstitutionManager::AutoSubstitute(
    int64_t original_id, const std::vector<int64_t>& waitlist,
    const std::unordered_map<int64_t, TournamentPlayer>& players) {
  if (waitlist.empty()) return std::nullopt;

  // 选等候名单中手牌最多的玩家 (最有经验)
  int64_t best_candidate = -1;
  double best_score = -1.0;

  for (int64_t candidate : waitlist) {
    auto it = players.find(candidate);
    if (it == players.end()) continue;

    double score = SubstituteScore(candidate, original_id, players);
    if (score > best_score) {
      best_score = score;
      best_candidate = candidate;
    }
  }

  if (best_candidate >= 0) {
    RequestSubstitution(original_id, best_candidate);
  }

  return best_candidate >= 0 ? std::make_optional(best_candidate) : std::nullopt;
}

bool SubstitutionManager::ApproveSubstitution(int64_t original_id, int64_t substitute_id) {
  for (auto& req : history_) {
    if (req.original_player_id == original_id && req.substitute_player_id == substitute_id) {
      req.approved = true;
      return true;
    }
  }
  return false;
}

double SubstitutionManager::SubstituteScore(
    int64_t candidate_id, int64_t original_id,
    const std::unordered_map<int64_t, TournamentPlayer>& players) {
  auto it_c = players.find(candidate_id);
  auto it_o = players.find(original_id);

  if (it_c == players.end() || it_o == players.end()) return -1.0;

  const auto& cand = it_c->second;
  const auto& orig = it_o->second;

  // 经验评分 (手牌数)
  double experience = std::min(cand.hands_played / 100.0, 1.0);

  // 分数接近度 (筹码差距越小越好)
  double chip_diff = std::abs(cand.chips - orig.chips);
  double chip_similarity = 1.0 / (1.0 + chip_diff / orig.starting_stack);

  // 赢率 (如果可用)
  double win_rate =
      cand.hands_played > 0 ? cand.total_won / static_cast<double>(cand.hands_played) : 0.5;

  return experience * 0.4 + chip_similarity * 0.3 + win_rate * 0.3;
}

// ==================== TournamentBalancer ====================

TournamentBalancer::BalanceRecommendation TournamentBalancer::Analyze(
    const TournamentConfig& config, const std::vector<TournamentPlayer>& active_players,
    double avg_pot_recent, double avg_hand_time_seconds) {
  BalanceRecommendation rec;
  rec.should_merge_tables = false;
  rec.should_open_registration = false;

  int active_count = active_players.size();

  // 玩家不足时开放注册
  if (active_count < config_.min_players_for_reg_boost) {
    rec.should_open_registration = true;
    rec.reason = "Active players below threshold (" + std::to_string(active_count) + " < " +
                 std::to_string(config_.min_players_for_reg_boost) + ")";
  }

  // 动态 AI 数量
  int ai_count = config_.min_ai_players;
  if (active_count < config_.target_avg_pot && config_.target_avg_pot > 0) {
    ai_count = std::min(config_.max_ai_players,
                        config_.min_ai_players +
                            static_cast<int>(config_.min_players_for_reg_boost - active_count));
  }
  rec.suggested_ai_count = std::max(0, config.max_players - active_count);

  // 盲注调整
  double pot_ratio = avg_pot_recent / config_.target_avg_pot;
  if (pot_ratio > 1.5) {
    rec.suggested_blind_multiplier = static_cast<int>(std::pow(1.2, pot_ratio - 1.5));
    rec.reason += " | Pots running high, consider faster blinds";
  } else if (pot_ratio < 0.5) {
    rec.suggested_blind_multiplier = 0;  // 不要加速
    rec.reason += " | Pots running low, slow down blinds";
  } else {
    rec.suggested_blind_multiplier = 1;
  }

  // 桌子合并检测
  if (active_count <= 6 && config.max_players > 6) {
    rec.should_merge_tables = true;
    rec.reason += " | Merging to final table";
  }

  return rec;
}

}  // namespace poker_engine::tournament
