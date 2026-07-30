#pragma once
#include <string>
#include <vector>

#include "poker_engine/network/ai_engine.h"

namespace poker_engine::examples {

// A REAL, optional LLM agent implementing the IAIEngine seam (ADR-004).
//
// It renders the *redacted* Observation (its own hole cards + public table
// state) plus the legal action set into a text prompt, calls an
// OpenAI-compatible chat-completions endpoint over HTTPS, and maps the reply
// back onto exactly one of the legal actions. Configuration is env-gated:
//
//   OPENAI_API_KEY   required. Without it the agent never makes a network call
//                    and falls back to a safe passive action (check/call/fold).
//   OPENAI_BASE_URL  optional, default "https://api.openai.com".
//   OPENAI_MODEL     optional, default "gpt-4o-mini".
//
// Any transport, HTTP, or parse error also degrades to the same safe fallback,
// so a match never crashes on a flaky model. Amounts stay in Chips (int64
// cents); the model only ever *selects* among legal actions and, for bet/raise,
// proposes a size that is clamped into the legal [min, all-in] range.
class LlmAgent : public network::IAIEngine {
 public:
  explicit LlmAgent(const network::AIConfig& config = network::AIConfig());

  void Initialize(const network::AIConfig& config) override;
  network::DecisionResponse Decide(const network::DecisionRequest& request) override;
  void OnHandComplete(const game::GameState& final_state) override;
  bool ReloadModel(const std::string& model_path) override;
  std::string Name() const override { return config_.name; }
  network::AIDifficulty Difficulty() const override { return config_.difficulty; }

  // True when OPENAI_API_KEY was present at construction (i.e. real LLM calls
  // will be attempted). Useful for demos to report which mode they ran in.
  bool HasApiKey() const { return !api_key_.empty(); }

 private:
  network::AIConfig config_;
  std::string api_key_;
  std::string base_url_;
  std::string model_;
  bool warned_no_key_ = false;

  void LoadEnv();

  // Render the redacted observation + legal actions into a user prompt.
  std::string BuildPrompt(const game::Observation& obs,
                          const std::vector<game::GameAction>& legal) const;

  // POST to <base_url>/v1/chat/completions; returns the assistant message text,
  // or an empty string on any error.
  std::string CallLlm(const std::string& prompt);

  // Parse the assistant text (expected: {"action": "...", "amount": N}) and map
  // it onto one legal action, clamping bet/raise sizes. Returns the chosen
  // action, or the safe fallback if parsing fails or nothing matches.
  game::GameAction MapReplyToAction(const std::string& reply,
                                    const std::vector<game::GameAction>& legal,
                                    const game::Observation& obs,
                                    std::string* reason) const;

  // Safe passive choice: prefer CHECK, then CALL, then FOLD, else first legal.
  game::GameAction SafeFallback(const std::vector<game::GameAction>& legal) const;
};

}  // namespace poker_engine::examples
