#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
class Server;
}

namespace poker_engine::network {

struct ClientInfo {
  int id = -1;
  std::string session_token;
  int player_id = -1;
  std::string ip;
};

// Real WebSocket server backed by cpp-httplib.
// Each WS connection gets a reader thread that calls on_message_.
// SendTo/Broadcast are thread-safe.
class WebSocketServer {
 public:
  explicit WebSocketServer(int port = 8080);
  ~WebSocketServer();

  bool Start();
  void Stop();
  bool IsRunning() const;

  using MessageHandler = std::function<void(int client_id, const std::string& msg)>;
  using ConnectHandler = std::function<void(int client_id)>;
  using DisconnectHandler = std::function<void(int client_id)>;

  void OnMessage(MessageHandler handler);
  void OnConnect(ConnectHandler handler);
  void OnDisconnect(DisconnectHandler handler);

  void SendTo(int client_id, const std::string& msg);
  void Broadcast(const std::string& msg);
  void BroadcastToTable(const std::string& table_id, const std::string& msg);

  void CreateTable(const std::string& table_id, const std::string& config_json);
  void CloseTable(const std::string& table_id);

  int ConnectedClients() const;

 private:
  void ReaderLoop(int client_id);

  int port_;
  std::atomic<bool> running_{false};

  // httplib server (owns the listening thread)
  std::unique_ptr<httplib::Server> server_;
  std::thread server_thread_;

  // Per-client state
  struct ClientConn {
    ClientInfo info;
    void* ws_ptr = nullptr;  // ws::WebSocket* — type-erased to avoid httplib header
    std::thread reader;
    std::atomic<bool> alive{true};
  };
  std::map<int, std::unique_ptr<ClientConn>> clients_;
  mutable std::mutex clients_mutex_;

  std::map<int, std::string> client_to_table_;
  std::map<std::string, std::vector<int>> table_to_clients_;
  mutable std::mutex table_mutex_;

  std::atomic<int> next_client_id_{0};

  MessageHandler on_message_;
  ConnectHandler on_connect_;
  DisconnectHandler on_disconnect_;
};

}  // namespace poker_engine::network
