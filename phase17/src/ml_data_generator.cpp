#include "poker_engine/anticheat/ml_data_generator.h"

#include <algorithm>
#include <cmath>

#include "poker_engine/base/logging.h"

namespace poker_engine::anticheat {

MLDataGenerator::MLDataGenerator(uint32_t seed) : rng_(seed) {}

// ==================== 辅助 ====================

double MLDataGenerator::NormalDistribution(double mean, double stddev) {
  std::normal_distribution<double> dist(mean, stddev);
  return dist(rng_);
}

double MLDataGenerator::Clamp(double value, double min_val, double max_val) {
  return std::max(min_val, std::min(max_val, value));
}

void MLDataGenerator::AddNoise(PlayerFeatureVector& fv, double noise_level) {
  std::normal_distribution<double> dist(0, noise_level);
  for (int i = 0; i < PlayerFeatureVector::kFeatureCount; ++i) {
    fv[i] += dist(rng_);
    fv[i] = Clamp(fv[i], 0.0, 1.0);
  }
}

// ==================== 人类玩家 ====================

std::pair<PlayerFeatureVector, int> MLDataGenerator::GenerateHumanSample(int hands_min,
                                                                         int hands_max) {
  PlayerFeatureVector fv;
  fv.player_id = -1;
  std::fill(std::begin(fv.features), std::end(fv.features), 0.0);

  // 手牌数
  std::uniform_real_distribution<double> uniform(0, 1);
  double log_hands = NormalDistribution(5.0, 1.0);
  int hands = static_cast<int>(std::exp(log_hands));
  hands = std::max(hands_min, std::min(hands_max, hands));
  fv[PlayerFeatureVector::FI_HANDS_PLAYED] = Clamp(hands / 1000.0, 0.0, 1.0);

  // VPIP（松紧度）
  double vpip = NormalDistribution(0.27, 0.08);
  vpip = Clamp(vpip, 0.08, 0.60);
  fv[PlayerFeatureVector::FI_VPIP] = vpip;

  // PFR（翻前加注率）
  double pfr_ratio = NormalDistribution(0.72, 0.12);
  pfr_ratio = Clamp(pfr_ratio, 0.3, 1.0);
  fv[PlayerFeatureVector::FI_PFR] = vpip * pfr_ratio;

  // 侵略因子
  double af = NormalDistribution(1.5, 0.6);
  af = Clamp(af, 0.3, 4.0);
  fv[PlayerFeatureVector::FI_AGG_FACTOR] = Clamp(af / 5.0, 0.0, 1.0);

  // 赢率
  double win_rate = NormalDistribution(0.05, 0.10);
  win_rate = Clamp(win_rate, 0.0, 0.6);
  fv[PlayerFeatureVector::FI_WIN_RATE] = win_rate;

  // 三加注率
  double three_bet = NormalDistribution(0.06, 0.04);
  three_bet = Clamp(three_bet, 0.005, 0.25);
  fv[PlayerFeatureVector::FI_THREE_BET_PCT] = three_bet;

  // 底池比率（平均下注/底池）
  double avg_pot_pct = NormalDistribution(0.6, 0.2);
  avg_pot_pct = Clamp(avg_pot_pct, 0.1, 1.5);
  fv[PlayerFeatureVector::FI_AVG_POT_PCT] = avg_pot_pct;

  // 响应时间（人类特征：对数正态分布）
  double log_rt = NormalDistribution(7.0, 0.8);  // e^7 ≈ 1096ms
  double mean_rt = std::exp(log_rt);
  double rt_stddev = mean_rt * NormalDistribution(0.6, 0.2);
  rt_stddev = std::max(100.0, rt_stddev);

  fv[PlayerFeatureVector::FI_RESPONSE_TIME_MEAN] = Clamp(mean_rt / 30000.0, 0.0, 1.0);
  fv[PlayerFeatureVector::FI_RESPONSE_TIME_STDDEV] = Clamp(rt_stddev / 30000.0, 0.0, 1.0);
  fv[PlayerFeatureVector::FI_RESPONSE_TIME_CV] = Clamp(rt_stddev / mean_rt, 0.0, 2.0);

  // 下注尺度
  double bet_size = NormalDistribution(0.65, 0.2);
  bet_size = Clamp(bet_size, 0.1, 1.5);
  fv[PlayerFeatureVector::FI_BET_SIZE_MEAN] = bet_size;
  fv[PlayerFeatureVector::FI_BET_SIZE_STDDEV] = NormalDistribution(0.2, 0.1);

  // 位置特征（人类在不同位置打法不同）
  fv[PlayerFeatureVector::FI_POSITIONAL_VPIP_EP] =
      Clamp(vpip * 0.6 + NormalDistribution(0, 0.05), 0.0, 1.0);
  fv[PlayerFeatureVector::FI_POSITIONAL_VPIP_MP] =
      Clamp(vpip * 0.8 + NormalDistribution(0, 0.05), 0.0, 1.0);
  fv[PlayerFeatureVector::FI_POSITIONAL_VPIP_LP] =
      Clamp(vpip * 1.1 + NormalDistribution(0, 0.05), 0.0, 1.0);
  fv[PlayerFeatureVector::FI_POSITIONAL_VPIP_BLIND] =
      Clamp(vpip * 1.2 + NormalDistribution(0, 0.08), 0.0, 1.0);

  // 同桌率（正常玩家通常较低）
  fv[PlayerFeatureVector::FI_SAME_TABLE_TOP1_RATE] = Clamp(uniform(rng_) * 0.2, 0.0, 1.0);
  fv[PlayerFeatureVector::FI_ADJACENT_SEAT_TOP1_RATE] = Clamp(uniform(rng_) * 0.15, 0.0, 1.0);

  // WTSD
  double wtsd = NormalDistribution(0.28, 0.08);
  wtsd = Clamp(wtsd, 0.05, 0.60);
  fv[PlayerFeatureVector::FI_WTSD_PCT] = wtsd;

  // WMSD
  double wmsd = NormalDistribution(0.50, 0.15);
  wmsd = Clamp(wmsd, 0.10, 0.90);
  fv[PlayerFeatureVector::FI_WMSD_PCT] = wmsd;

  AddNoise(fv, 0.02);

  return {fv, -1};  // label = -1 (正常)
}

// ==================== 反应时间 Bot ====================

std::pair<PlayerFeatureVector, int> MLDataGenerator::GenerateReactionBotSample() {
  auto [fv, _] = GenerateHumanSample();

  // 设置恒定响应时间
  double fixed_rt = NormalDistribution(2.0, 0.0001);  // 几乎恒定 2 秒
  fixed_rt = Clamp(fixed_rt, 1.5, 2.5);

  fv[PlayerFeatureVector::FI_RESPONSE_TIME_MEAN] = fixed_rt / 30.0;
  fv[PlayerFeatureVector::FI_RESPONSE_TIME_STDDEV] = 0.001;  // 几乎为 0
  fv[PlayerFeatureVector::FI_RESPONSE_TIME_CV] = 0.001;

  AddNoise(fv, 0.01);

  return {fv, 1};  // label = 1 (Bot)
}

// ==================== Solver Bot ====================

std::pair<PlayerFeatureVector, int> MLDataGenerator::GenerateSolverBotSample() {
  auto [fv, _] = GenerateHumanSample();

  // 过于最优：非常高的 PFR/VPIP 比率
  fv[PlayerFeatureVector::FI_PFR] =
      fv[PlayerFeatureVector::FI_VPIP] * NormalDistribution(0.95, 0.02);
  fv[PlayerFeatureVector::FI_PFR] = Clamp(fv[PlayerFeatureVector::FI_PFR], 0.0, 1.0);

  // 位置差异小（Solver 在所有位置使用类似策略）
  double vpip = fv[PlayerFeatureVector::FI_VPIP];
  for (int i = PlayerFeatureVector::FI_POSITIONAL_VPIP_EP;
       i <= PlayerFeatureVector::FI_POSITIONAL_VPIP_BLIND; ++i) {
    fv[i] = vpip * (1.0 + NormalDistribution(0, 0.01));
    fv[i] = Clamp(fv[i], 0.0, 1.0);
  }

  // 下注尺度高度精确
  fv[PlayerFeatureVector::FI_BET_SIZE_STDDEV] = 0.001;

  AddNoise(fv, 0.01);

  return {fv, 1};  // label = 1 (Bot)
}

// ==================== 串通 Bot 对 ====================

std::pair<std::pair<PlayerFeatureVector, int>, std::pair<PlayerFeatureVector, int>>
MLDataGenerator::GenerateCollusionBotPair() {
  auto [fv1, _] = GenerateHumanSample();
  auto [fv2, __] = GenerateHumanSample();

  // 高同桌率
  fv1[PlayerFeatureVector::FI_SAME_TABLE_TOP1_RATE] = Clamp(NormalDistribution(0.7, 0.1), 0.3, 1.0);
  fv2[PlayerFeatureVector::FI_SAME_TABLE_TOP1_RATE] =
      fv1[PlayerFeatureVector::FI_SAME_TABLE_TOP1_RATE];

  // 高相邻座率
  fv1[PlayerFeatureVector::FI_ADJACENT_SEAT_TOP1_RATE] =
      Clamp(NormalDistribution(0.5, 0.1), 0.2, 1.0);
  fv2[PlayerFeatureVector::FI_ADJACENT_SEAT_TOP1_RATE] =
      fv1[PlayerFeatureVector::FI_ADJACENT_SEAT_TOP1_RATE];

  // Soft play: 低侵略因子
  fv1[PlayerFeatureVector::FI_AGG_FACTOR] = Clamp(NormalDistribution(0.3, 0.1), 0.05, 0.6);
  fv2[PlayerFeatureVector::FI_AGG_FACTOR] = fv1[PlayerFeatureVector::FI_AGG_FACTOR];

  // 异乎寻常低的 VPIP
  double low_vpip = Clamp(NormalDistribution(0.10, 0.03), 0.02, 0.20);
  fv1[PlayerFeatureVector::FI_VPIP] = low_vpip;
  fv2[PlayerFeatureVector::FI_VPIP] = low_vpip;

  AddNoise(fv1, 0.02);
  AddNoise(fv2, 0.02);

  return {{fv1, 1}, {fv2, 1}};
}

// ==================== 批量生成 ====================

std::vector<std::pair<PlayerFeatureVector, int>> MLDataGenerator::GenerateDataset(
    int human_samples, int reaction_bot_samples, int solver_bot_samples, int collusion_pairs) {
  std::vector<std::pair<PlayerFeatureVector, int>> dataset;

  // 正常玩家
  for (int i = 0; i < human_samples; ++i) {
    dataset.push_back(GenerateHumanSample());
  }

  // 反应时间 Bot
  for (int i = 0; i < reaction_bot_samples; ++i) {
    dataset.push_back(GenerateReactionBotSample());
  }

  // Solver Bot
  for (int i = 0; i < solver_bot_samples; ++i) {
    dataset.push_back(GenerateSolverBotSample());
  }

  // 串通 Bot
  for (int i = 0; i < collusion_pairs; ++i) {
    auto pair = GenerateCollusionBotPair();
    dataset.push_back(pair.first);
    dataset.push_back(pair.second);
  }

  // 打乱
  std::shuffle(dataset.begin(), dataset.end(), rng_);

  PE_LOG_INFO(
      "Generated dataset: {} samples ({} human, {} reaction-bot, "
      "{} solver-bot, {} collusion-pairs)",
      dataset.size(), human_samples, reaction_bot_samples, solver_bot_samples, collusion_pairs);

  return dataset;
}

}  // namespace poker_engine::anticheat
