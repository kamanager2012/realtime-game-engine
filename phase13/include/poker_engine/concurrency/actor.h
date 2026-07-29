#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>

#include "poker_engine/base/logging.h"

namespace poker_engine::concurrency {

struct MessageEnvelope {
  uint64_t sender_id;
  uint64_t target_actor_id;
  std::string message_type;
  std::string payload;
  std::promise<void>* ack_promise = nullptr;
};

class Actor {
 public:
  using MessageHandler = std::function<void(const MessageEnvelope&)>;

  explicit Actor(uint64_t actor_id) : actor_id_(actor_id), running_(false) {}

  virtual ~Actor() { Stop(); }

  uint64_t GetId() const { return actor_id_; }

  virtual void Start() {
    running_ = true;
    thread_ = std::thread([this]() { RunLoop(); });
  }

  virtual void Stop() {
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    Drain();
  }

  void Tell(MessageEnvelope msg) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      mailbox_.push(std::move(msg));
    }
    cv_.notify_one();
  }

  void Ask(MessageEnvelope msg) {
    std::promise<void> promise;
    msg.ack_promise = &promise;
    Tell(std::move(msg));
    promise.get_future().wait();
  }

  void RegisterHandler(const std::string& msg_type, MessageHandler handler) {
    handlers_[msg_type] = std::move(handler);
  }

  void SetDefaultHandler(MessageHandler handler) { default_handler_ = std::move(handler); }

 protected:
  virtual void OnReceive(const MessageEnvelope& msg) {
    auto it = handlers_.find(msg.message_type);
    if (it != handlers_.end()) {
      it->second(msg);
    } else if (default_handler_) {
      default_handler_(msg);
    } else {
      PE_LOG_WARN("Actor {}: unhandled message type '{}'", actor_id_, msg.message_type);
    }
  }

 private:
  void RunLoop() {
    while (running_) {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      cv_.wait(lock, [this]() { return !mailbox_.empty() || !running_; });

      if (!running_ && mailbox_.empty()) break;

      if (!mailbox_.empty()) {
        auto msg = std::move(mailbox_.front());
        mailbox_.pop();
        lock.unlock();

        try {
          OnReceive(msg);
        } catch (const std::exception& e) {
          PE_LOG_ERROR("Actor {}: exception processing '{}': {}", actor_id_, msg.message_type,
                       e.what());
        }

        if (msg.ack_promise) {
          msg.ack_promise->set_value();
        }
      }
    }
  }

  void Drain() {
    while (!mailbox_.empty()) {
      auto msg = std::move(mailbox_.front());
      mailbox_.pop();
      try {
        OnReceive(msg);
      } catch (...) {
      }
      if (msg.ack_promise) msg.ack_promise->set_value();
    }
  }

  uint64_t actor_id_;
  std::thread thread_;
  std::atomic<bool> running_;

  std::queue<MessageEnvelope> mailbox_;
  std::mutex queue_mutex_;
  std::condition_variable cv_;

  std::unordered_map<std::string, MessageHandler> handlers_;
  MessageHandler default_handler_;
};

class ActorSystem {
 public:
  static ActorSystem& Instance() {
    static ActorSystem instance;
    return instance;
  }

  void Register(std::unique_ptr<Actor> actor) {
    uint64_t id = actor->GetId();
    actors_[id] = std::move(actor);
  }

  void Start(uint64_t actor_id) {
    auto it = actors_.find(actor_id);
    if (it != actors_.end()) {
      it->second->Start();
      PE_LOG_INFO("Actor {} started", actor_id);
    }
  }

  void Stop(uint64_t actor_id) {
    auto it = actors_.find(actor_id);
    if (it != actors_.end()) {
      it->second->Stop();
      PE_LOG_INFO("Actor {} stopped", actor_id);
    }
  }

  void Tell(uint64_t target, MessageEnvelope msg) {
    auto it = actors_.find(target);
    if (it != actors_.end()) {
      it->second->Tell(std::move(msg));
    } else {
      PE_LOG_WARN("Actor {} not found for Tell", target);
    }
  }

  void Ask(uint64_t target, MessageEnvelope msg) {
    auto it = actors_.find(target);
    if (it != actors_.end()) {
      it->second->Ask(std::move(msg));
    } else {
      PE_LOG_WARN("Actor {} not found for Ask", target);
    }
  }

  void Shutdown() {
    for (auto& [id, _] : actors_) {
      Stop(id);
    }
    actors_.clear();
  }

  size_t ActorCount() const { return actors_.size(); }

  Actor* FindActor(uint64_t id) {
    auto it = actors_.find(id);
    return it != actors_.end() ? it->second.get() : nullptr;
  }

  ~ActorSystem() { Shutdown(); }

 private:
  ActorSystem() = default;
  std::unordered_map<uint64_t, std::unique_ptr<Actor>> actors_;
};

class ActorRef {
 public:
  ActorRef(uint64_t actor_id) : actor_id_(actor_id) {}

  void Tell(MessageEnvelope msg) const { ActorSystem::Instance().Tell(actor_id_, std::move(msg)); }

  void Ask(MessageEnvelope msg) const { ActorSystem::Instance().Ask(actor_id_, std::move(msg)); }

  uint64_t GetId() const { return actor_id_; }

 private:
  uint64_t actor_id_;
};

}  // namespace poker_engine::concurrency
