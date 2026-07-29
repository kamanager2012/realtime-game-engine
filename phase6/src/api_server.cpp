#include "poker_engine/phase6/api_server.h"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "poker_engine/equity/equity_calculator.h"
#include "poker_engine/range/range.h"

namespace poker_engine {
namespace phase6 {
using namespace poker_engine::range;
using namespace poker_engine::equity;
using namespace poker_engine::evaluator;

// ===================== JSON helpers =====================

std::string APIResponse::JSONResponse(int code, const std::string& message,
                                      const std::string& data) {
  std::ostringstream oss;
  oss << "{\"status\":" << code << ",\"message\":\"" << message << "\""
      << (data.empty() ? "" : ",\"data\":" + data) << "}";
  return oss.str();
}

std::map<std::string, std::string> APIResponse::ParseQuery(const std::string& query) {
  std::map<std::string, std::string> result;
  std::istringstream iss(query);
  std::string pair;
  while (std::getline(iss, pair, '&')) {
    size_t eq = pair.find('=');
    if (eq != std::string::npos) {
      std::string key = pair.substr(0, eq);
      std::string val = pair.substr(eq + 1);
      result[key] = val;
    }
  }
  return result;
}

// ===================== APIServer =====================

#ifdef HAS_CPPHTTPLIB

APIServer::APIServer(const APIConfig& config) : config_(config) {}
APIServer::~APIServer() { Stop(); }

void APIServer::OnGet(
    const std::string& path,
    std::function<APIResponse(const std::map<std::string, std::string>&)> handler) {
  svr_.Get(path.c_str(), [handler](const httplib::Request& req, httplib::Response& res) {
    std::map<std::string, std::string> params;
    for (const auto& [k, v] : req.params) params[k] = v;
    auto response = handler(params);
    res.status = response.status_code;
    res.set_header("Content-Type", response.content_type.c_str());
    res.set_content(response.body, "text/plain");
  });
}

void APIServer::OnPost(const std::string& path,
                       std::function<APIResponse(const std::string&)> handler) {
  svr_.Post(path.c_str(), [handler](const httplib::Request& req, httplib::Response& res) {
    auto response = handler(req.body);
    res.status = response.status_code;
    res.set_header("Content-Type", response.content_type.c_str());
    res.set_content(response.body, "text/plain");
  });
}

void APIServer::RegisterDefaultHandlers() {
  OnGet("/health", [this](auto&) { return HandleHealth({}); });
  OnPost("/parse", [this](auto& b) { return HandleParse(b); });
  OnPost("/equity", [this](auto& b) { return HandleEquity(b); });
  OnPost("/icm", [this](auto& b) { return HandleICM(b); });
  OnPost("/range", [this](auto& b) { return HandleRange(b); });
  OnPost("/solve", [this](auto& b) { return HandleSolve(b); });
  OnGet("/stats", [this](auto&) { return HandleStats(""); });
}

bool APIServer::Start() {
  RegisterDefaultHandlers();
  running_ = svr_.listen(config_.host.c_str(), config_.port);
  return running_;
}

void APIServer::Stop() {
  if (running_) {
    svr_.stop();
    running_ = false;
  }
}

void APIServer::SetDatabase(std::shared_ptr<HandDatabase> db) { db_ = db; }
void APIServer::SetSolver(std::shared_ptr<ICFRSolver> solver) { solver_ = solver; }

// ============ Handler Implementations ============

APIResponse APIServer::HandleHealth(const std::map<std::string, std::string>&) {
  std::string data =
      "{\"db_connected\":" + std::string(db_ && db_->IsOpen() ? "true" : "false") + "}";
  return APIResponse{200, "OK", data};
}

APIResponse APIServer::HandleParse(const std::string& body) {
  poker_engine::phase4::HandHistoryParser parser;
  auto hh = parser.Parse(body);
  if (hh.hand_id == 0) return APIResponse{400, "Parse failed"};

  std::ostringstream data;
  data << std::fixed << std::setprecision(2);
  data << "{\"hand_id\":" << hh.hand_id << ",\"hero\":\"" << hh.HeroName() << "\""
       << ",\"pot\":" << hh.total_pot << ",\"board\":\"" << hh.BoardString() << "\"}";

  return APIResponse{200, "Parsed", data.str()};
}

APIResponse APIServer::HandleEquity(const std::string& body) {
  auto params = APIResponse::ParseQuery(body);
  std::string hero = params.count("hero") ? params["hero"] : "AKs";
  std::string villain = params.count("villain") ? params["villain"] : "22+";

  auto hero_r = Range::FromString(hero);
  auto villain_r = Range::FromString(villain);

  std::mt19937 rng(42);
  uint8_t board5[5] = {0};
  int bs = 0;

  if (params.count("flop")) {
    std::string flop = params["flop"];
    for (size_t i = 0; i + 1 < flop.size() && bs < 5; i += 2) {
      board5[bs++] = Card::Parse(flop.substr(i, 2)).Id();
    }
  }

  auto res = EquityCalculator::CalculateMonteCarlo(hero_r, villain_r, board5, bs, 20000, rng);

  std::ostringstream data;
  data << std::fixed << std::setprecision(2);
  data << "{\"hero_equity\":" << res.equity[0] << ",\"villain_equity\":" << res.equity[1]
       << ",\"tie\":" << res.tie << "}";

  return APIResponse{200, "OK", data.str()};
}

APIResponse APIServer::HandleICM(const std::string& body) {
  auto params = APIResponse::ParseQuery(body);
  if (!params.count("payouts") || !params.count("chips"))
    return APIResponse{400, "Missing payouts/chips params"};

  auto parseVec = [](const std::string& s) -> std::vector<double> {
    std::vector<double> v;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) v.push_back(std::stod(token));
    return v;
  };

  auto payouts = parseVec(params["payouts"]);
  auto chips = parseVec(params["chips"]);

  poker_engine::phase5::ICMCalculator calc(payouts);
  for (size_t i = 0; i < chips.size(); i++) calc.AddPlayer("P" + std::to_string(i + 1), chips[i]);

  auto result = calc.Calculate();

  std::ostringstream data;
  data << std::fixed << std::setprecision(2);
  data << "{";
  for (size_t i = 0; i < result.players.size(); i++) {
    if (i > 0) data << ",";
    data << "\"" << result.players[i].name << "\":" << result.players[i].equity;
  }
  data << "}";

  return APIResponse{200, "OK", data.str()};
}

APIResponse APIServer::HandleRange(const std::string& body) {
  auto params = APIResponse::ParseQuery(body);
  if (!params.count("range")) return APIResponse{400, "Missing range param"};

  auto range = Range::FromString(params["range"]);

  std::ostringstream data;
  data << "{\"combos\":" << range.NonZeroCount() << "}";
  return APIResponse{200, "OK", data.str()};
}

APIResponse APIServer::HandleSolve(const std::string& body) {
  auto params = APIResponse::ParseQuery(body);
  std::string hero = params.count("hero") ? params["hero"] : "AKs";
  std::string villain = params.count("villain") ? params["villain"] : "22+";
  std::string flop = params.count("flop") ? params["flop"] : "";

  ICFRConfig config;
  config.iterations = 500;
  config.verbose = false;

  ICFRSolver solver(config);
  solver.SetHeroRange(Range::FromString(hero));
  solver.SetVillainRange(Range::FromString(villain));

  if (!flop.empty()) {
    std::vector<Card> board;
    for (size_t i = 0; i + 1 < flop.size(); i += 2) board.push_back(Card::Parse(flop.substr(i, 2)));
    solver.SetBoard(board);
  }

  auto result = solver.Solve();

  std::ostringstream data;
  data << std::fixed << std::setprecision(3);
  data << "{\"nodes\":" << result.strategy_profile.size()
       << ",\"exploitability\":" << result.exploitability_vs_blueprint
       << ",\"ev\":" << result.achieved_ev << "}";

  return APIResponse{200, "OK", data.str()};
}

APIResponse APIServer::HandleStats(const std::string&) {
  std::ostringstream data;
  if (db_ && db_->IsOpen()) {
    int64_t total = db_->TotalHands();
    data << "{\"hands_total\":" << total << ",\"db_active\":true}";
  } else {
    data << "{\"hands_total\":0,\"db_active\":false}";
  }
  return APIResponse{200, "OK", data.str()};
}

#else  // No cpp-httplib

APIServer::APIServer(const APIConfig& config) : config_(config) {}
APIServer::~APIServer() {}
void APIServer::OnGet(const std::string&,
                      std::function<APIResponse(const std::map<std::string, std::string>&)>) {}
void APIServer::OnPost(const std::string&, std::function<APIResponse(const std::string&)>) {}
void APIServer::RegisterDefaultHandlers() {}
bool APIServer::Start() { return false; }
void APIServer::Stop() {}
void APIServer::SetDatabase(std::shared_ptr<HandDatabase>) {}
void APIServer::SetSolver(std::shared_ptr<ICFRSolver>) {}

APIResponse APIServer::HandleHealth(const std::map<std::string, std::string>&) { return {}; }
APIResponse APIServer::HandleParse(const std::string&) { return {}; }
APIResponse APIServer::HandleEquity(const std::string&) { return {}; }
APIResponse APIServer::HandleICM(const std::string&) { return {}; }
APIResponse APIServer::HandleRange(const std::string&) { return {}; }
APIResponse APIServer::HandleSolve(const std::string&) { return {}; }
APIResponse APIServer::HandleStats(const std::string&) { return {}; }

#endif  // HAS_CPPHTTPLIB

// ===================== APIClient =====================

#ifdef HAS_CPPHTTPLIB

APIClient::APIClient(const std::string& host, int port) : host_(host), port_(port) {}

APIResponse APIClient::Get(const std::string& path) {
  httplib::Client cli(host_.c_str(), port_);
  auto res = cli.Get(path.c_str());
  if (res) return {res->status, res->body};
  return {503, "Service unavailable"};
}

APIResponse APIClient::Post(const std::string& path, const std::string& body) {
  httplib::Client cli(host_.c_str(), port_);
  auto res = cli.Post(path.c_str(), body, "application/x-www-form-urlencoded");
  if (res) return {res->status, res->body};
  return {503, "Service unavailable"};
}

#else

APIClient::APIClient(const std::string&, int) {}
APIResponse APIClient::Get(const std::string&) { return {503, "No HTTP library"}; }
APIResponse APIClient::Post(const std::string&, const std::string&) {
  return {503, "No HTTP library"};
}

#endif

}  // namespace phase6
}  // namespace poker_engine
