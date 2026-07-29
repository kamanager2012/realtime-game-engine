#include <cstdlib>
#include <iostream>
#include <string>

#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/cfr/cfr_model.h"
#include "poker_engine/cfr/cfr_training.h"

namespace {

void usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [--iterations N] [--output PATH] [--mode bootstrap|train]\n"
            << "  bootstrap (default): build heuristic CFR table for live bots\n"
            << "  train: run CFR iterations (experimental)\n";
}

void set_strategy(poker_engine::cfr::CFRNode* node, const double probs[5]) {
  for (int a = 0; a < poker_engine::cfr::action_count(); ++a) {
    node->strategy_sum[a] = probs[a] * 1000.0;
    node->regret_sum[a] = 0;
  }
  node->times_visited = 1000;
  node->compute_strategy();
}

bool bootstrap_model(const std::string& output) {
  poker_engine::cfr::CFREngine engine;
  engine.Initialize();

  const int pot_levels = engine.Options().pot_quantization;
  for (uint16_t bucket = 0; bucket < poker_engine::cfr::HandAbstraction::kNumBuckets; ++bucket) {
    const double strength = 1.0 - static_cast<double>(bucket) / 168.0;
    double probs[5] = {0.05, 0.35, 0.15, 0.25, 0.20};
    if (strength > 0.7) {
      probs[0] = 0.02; probs[1] = 0.25; probs[2] = 0.18; probs[3] = 0.35; probs[4] = 0.20;
    } else if (strength < 0.35) {
      probs[0] = 0.35; probs[1] = 0.40; probs[2] = 0.08; probs[3] = 0.10; probs[4] = 0.07;
    }

    for (uint8_t street = 0; street <= 3; ++street) {
      for (int pot = 0; pot < pot_levels; ++pot) {
        for (uint8_t bet = 0; bet < 4; ++bet) {
          for (uint8_t seat = 0; seat < 6; ++seat) {
            poker_engine::cfr::InfosetKey key{bucket, street, static_cast<uint16_t>(pot), bet, seat};
            auto* node = engine.GetOrCreateNode(key);
            set_strategy(node, probs);
          }
        }
      }
    }
  }

  if (engine.NodeCount() == 0) return false;
  return poker_engine::cfr::CFRModelIO::Save(output, engine.Nodes(), 0.25);
}

bool train_model(int iterations, const std::string& output) {
  poker_engine::cfr::CFROptions opts;
  opts.config.num_iterations = iterations;
  opts.config.discount_interval = 50.0;
  opts.exploitability_threshold = 0.0;
  opts.check_interval = iterations + 1;

  poker_engine::cfr::CFRTrainer trainer(opts);
  trainer.Train(iterations);
  if (trainer.NodeCount() == 0) return false;
  return trainer.SaveModel(output);
}

}  // namespace

int main(int argc, char* argv[]) {
  int iterations = 500;
  std::string output = "data/bot_policy.cfr";
  std::string mode = "bootstrap";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return 0;
    }
    if (arg == "--iterations" && i + 1 < argc) {
      iterations = std::atoi(argv[++i]);
      continue;
    }
    if (arg == "--output" && i + 1 < argc) {
      output = argv[++i];
      continue;
    }
    if (arg == "--mode" && i + 1 < argc) {
      mode = argv[++i];
      continue;
    }
    std::cerr << "Unknown argument: " << arg << "\n";
    usage(argv[0]);
    return 1;
  }

  std::cout << "[cfr_train] mode=" << mode << " output=" << output << "\n";
  bool ok = false;
  if (mode == "train") {
    ok = train_model(iterations, output);
  } else {
    ok = bootstrap_model(output);
  }

  if (!ok) {
    std::cerr << "[cfr_train] ERROR: failed to build model\n";
    return 2;
  }

  if (auto info = poker_engine::cfr::CFRModelIO::GetInfo(output)) {
    std::cout << "[cfr_train] Saved " << info->node_count << " nodes -> " << output << "\n";
  }
  std::cout << "[cfr_train] export POKER_CFR_MODEL_PATH=" << output << "\n";
  return 0;
}
