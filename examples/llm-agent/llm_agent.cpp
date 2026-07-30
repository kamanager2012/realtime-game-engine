// Optional real LLM agent. Compiled only when BUILD_EXAMPLES=ON.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include "llm_agent.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include <nlohmann/json.hpp>

#include "httplib.h"
#include "poker_engine/game/action.h"
#include "poker_engine/game/observation.h"

namespace poker_engine::examples {

using game::ActionType;
using game::Chips;
using game::GameAction;
using game::Observation;
using game::PlayerView;
using json = nlohmann::json;

namespace {

const char* ActionTypeName(ActionType t) {
  switch (t) {
    case ActionType::FOLD: return "FOLD";
    case ActionType::CHECK: return "CHECK";
    case ActionType::CALL: return "CALL";
    case ActionType::BET: return "BET";
    case ActionType::RAISE: return "RAISE";
    case ActionType::ALL_IN: return "ALL_IN";
    default: return "OTHER";
  }
}

// Case-insensitive match of a model-provided action name to an ActionType.
bool NameMatches(const std::string& name, ActionType t) {
  std::string up;
  up.reserve(name.size());
  for (char c : name) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  return up == ActionTypeName(t);
}

std::string GetEnv(const char* key, const std::string& fallback) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : fallback;
}

// Split "https://host:port" (or "http://host") into a base usable by
// httplib::Client, which accepts a scheme://host[:port] string directly.
}  // namespace

LlmAgent::LlmAgent(const network::AIConfig& config) : config_(config) { LoadEnv(); }

void LlmAgent::Initialize(const network::AIConfig& config) {
  config_ = config;
  LoadEnv();
}

void LlmAgent::LoadEnv() {
  api_key_ = GetEnv("OPENAI_API_KEY", "");
  base_url_ = GetEnv("OPENAI_BASE_URL", "https://api.openai.com");
  model_ = GetEnv("OPENAI_MODEL", "gpt-4o-mini");
}

bool LlmAgent::ReloadModel(const std::string&) { return false; }

void LlmAgent::OnHandComplete(const game::GameState&) {}

GameAction LlmAgent::SafeFallback(const std::vector<GameAction>& legal) const {
  const GameAction* check = nullptr;
  const GameAction* call = nullptr;
  const GameAction* fold = nullptr;
  for (const auto& a : legal) {
    if (a.type == ActionType::CHECK && !check) check = &a;
    else if (a.type == ActionType::CALL && !call) call = &a;
    else if (a.type == ActionType::FOLD && !fold) fold = &a;
  }
  if (check) return *check;
  if (call) return *call;
  if (fold) return *fold;
  return legal.front();
}

std::string LlmAgent::BuildPrompt(const Observation& obs,
                                  const std::vector<GameAction>& legal) const {
  const PlayerView* me = obs.Me();
  const Chips my_chips = me ? me->chips : 0;
  const Chips my_bet = me ? me->bet_info.current_bet : 0;

  std::string s;
  s += "You are a No-Limit Texas Hold'em poker agent. Choose the best action.\n\n";
  s += "Phase: ";
  s += game::GamePhaseName[static_cast<uint8_t>(obs.phase)];
  s += "\n";
  s += "Your hole cards: " + obs.MyHoleCards().ToString() + "\n";
  s += "Community: " + obs.community.ToString() + "\n";
  s += "Pot: " + std::to_string(obs.pot) + " cents\n";
  s += "Current bet to match: " + std::to_string(obs.current_bet) + " cents\n";
  s += "Big blind: " + std::to_string(obs.big_blind) + " cents\n";
  s += "Your stack: " + std::to_string(my_chips) + " cents (already invested this street: " +
       std::to_string(my_bet) + ")\n\n";
  s += "Legal actions (choose exactly one action name):\n";
  for (const auto& a : legal) {
    s += "  - " + std::string(ActionTypeName(a.type));
    if (a.type == ActionType::BET || a.type == ActionType::RAISE) {
      const Chips all_in = my_chips + my_bet;
      s += " (min " + std::to_string(a.amount) + ", max " + std::to_string(all_in) + " cents)";
    }
    s += "\n";
  }
  s +=
      "\nReply with ONLY a JSON object, no prose:\n"
      "{\"action\": \"<ACTION_NAME>\", \"amount\": <cents>}\n"
      "For FOLD/CHECK/CALL/ALL_IN, set amount to 0. For BET/RAISE, amount is the\n"
      "TOTAL chips you put in for this action, within the min/max shown.\n";
  return s;
}

std::string LlmAgent::CallLlm(const std::string& prompt) {
  httplib::Client cli(base_url_.c_str());
  cli.set_connection_timeout(5, 0);
  cli.set_read_timeout(static_cast<int>(config_.time_limit_ms / 1000) + 5, 0);

  json body = {
      {"model", model_},
      {"temperature", 0.2},
      {"messages",
       json::array(
           {{{"role", "system"},
             {"content", "You are a poker engine. Respond only with the requested JSON."}},
            {{"role", "user"}, {"content", prompt}}})}};

  httplib::Headers headers = {{"Authorization", "Bearer " + api_key_}};
  auto res = cli.Post("/v1/chat/completions", headers, body.dump(), "application/json");
  if (!res || res->status < 200 || res->status >= 300) return "";

  json parsed = json::parse(res->body, nullptr, false);
  if (parsed.is_discarded()) return "";
  if (!parsed.contains("choices") || !parsed["choices"].is_array() ||
      parsed["choices"].empty()) {
    return "";
  }
  const auto& msg = parsed["choices"][0]["message"];
  if (!msg.contains("content") || !msg["content"].is_string()) return "";
  return msg["content"].get<std::string>();
}

GameAction LlmAgent::MapReplyToAction(const std::string& reply,
                                      const std::vector<GameAction>& legal,
                                      const Observation& obs, std::string* reason) const {
  json parsed = json::parse(reply, nullptr, false);
  if (parsed.is_discarded() || !parsed.contains("action") || !parsed["action"].is_string()) {
    if (reason) *reason = "unparseable reply -> safe fallback";
    return SafeFallback(legal);
  }
  const std::string name = parsed["action"].get<std::string>();

  const GameAction* match = nullptr;
  for (const auto& a : legal) {
    if (NameMatches(name, a.type)) {
      match = &a;
      break;
    }
  }
  if (!match) {
    if (reason) *reason = "illegal action '" + name + "' -> safe fallback";
    return SafeFallback(legal);
  }

  GameAction chosen = *match;
  if (chosen.type == ActionType::BET || chosen.type == ActionType::RAISE) {
    const PlayerView* me = obs.Me();
    const Chips all_in = me ? (me->chips + me->bet_info.current_bet) : chosen.amount;
    Chips amt = chosen.amount;  // legal minimum
    if (parsed.contains("amount") && parsed["amount"].is_number()) {
      amt = static_cast<Chips>(parsed["amount"].get<double>());
    }
    chosen.amount = std::clamp(amt, chosen.amount, all_in);
  }
  if (reason) *reason = "LLM chose " + std::string(ActionTypeName(chosen.type));
  return chosen;
}

network::DecisionResponse LlmAgent::Decide(const network::DecisionRequest& request) {
  network::DecisionResponse resp;
  const auto& legal = request.legal_actions;
  if (legal.empty()) {
    resp.action.type = ActionType::FOLD;
    resp.action.player_id = request.player_id;
    resp.reason = "no legal actions";
    return resp;
  }

  if (api_key_.empty()) {
    if (!warned_no_key_) {
      std::fprintf(stderr,
                   "[LlmAgent] OPENAI_API_KEY not set — playing a safe passive baseline "
                   "(no LLM calls).\n");
      warned_no_key_ = true;
    }
    resp.action = SafeFallback(legal);
    resp.action.player_id = request.player_id;
    resp.reason = "no api key -> safe fallback";
    return resp;
  }

  const std::string prompt = BuildPrompt(request.observation, legal);
  const std::string reply = CallLlm(prompt);
  if (reply.empty()) {
    resp.action = SafeFallback(legal);
    resp.action.player_id = request.player_id;
    resp.reason = "llm error/timeout -> safe fallback";
    return resp;
  }

  std::string reason;
  resp.action = MapReplyToAction(reply, legal, request.observation, &reason);
  resp.action.player_id = request.player_id;
  resp.confidence = 0.5f;
  resp.reason = reason;
  return resp;
}

}  // namespace poker_engine::examples
