#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "poker_engine/phase5/equity_matrix.h"
#include "poker_engine/phase5/icm_calc.h"
#include "poker_engine/phase6/hand_database.h"
#include "poker_engine/phase6/icfr_solver.h"

namespace poker_engine {
namespace phase6 {

// Lightweight REST API Server
// Requires cpp-httplib (header-only) - optional compilation

struct APIConfig {
  std::string host = "0.0.0.0";
  int port = 8080;
  int thread_count = 2;
};

struct APIResponse {
  int status_code = 200;
  std::string body;
  std::string content_type = "application/json";

  static std::string JSONResponse(int code, const std::string& message,
                                  const std::string& data = "");
  static std::map<std::string, std::string> ParseQuery(const std::string& query);
};

#ifdef HAS_CPPHTTPLIB
#include <httplib.h>
#endif

class APIServer {
 public:
  explicit APIServer(const APIConfig& config = APIConfig());
  ~APIServer();

  // Register handlers
  void OnGet(const std::string& path,
             std::function<APIResponse(const std::map<std::string, std::string>&)> handler);
  void OnPost(const std::string& path, std::function<APIResponse(const std::string& body)> handler);

  // Built-in handlers
  void RegisterDefaultHandlers();

  // Start/Stop server
  bool Start();
  void Stop();

  // Set data sources
  void SetDatabase(std::shared_ptr<HandDatabase> db);
  void SetSolver(std::shared_ptr<poker_engine::phase6::ICFRSolver> solver);

  bool IsRunning() const { return running_; }
  int GetPort() const { return config_.port; }

 private:
  APIConfig config_;
  bool running_ = false;
  std::shared_ptr<HandDatabase> db_;
  std::shared_ptr<poker_engine::phase6::ICFRSolver> solver_;

#ifdef HAS_CPPHTTPLIB
  httplib::Server svr_;
#endif

  // Handler implementations
  APIResponse HandleHealth(const std::map<std::string, std::string>&);
  APIResponse HandleParse(const std::string& body);
  APIResponse HandleEquity(const std::string& body);
  APIResponse HandleICM(const std::string& body);
  APIResponse HandleRange(const std::string& body);
  APIResponse HandleSolve(const std::string& body);
  APIResponse HandleStats(const std::string& body);
};

// Minimal HTTP client for tests
class APIClient {
 public:
  APIClient(const std::string& host, int port);
  APIResponse Get(const std::string& path);
  APIResponse Post(const std::string& path, const std::string& body = "");

 private:
  std::string host_;
  int port_;
};

}  // namespace phase6
}  // namespace poker_engine
