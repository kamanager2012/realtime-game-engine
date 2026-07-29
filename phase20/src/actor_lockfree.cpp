// Lock-Free Actor with batch message processing
// 替换 concurrency/actor.h 中的消息队列
// 将 std::queue + mutex + condition_variable 替换为 lock-free 队列

#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "poker_engine/base/logging.h"
#include "poker_engine/concurrency/lockfree_queue.h"

namespace poker_engine::concurrency {

// 支持批量处理的消息信封
struct alignas(64) AlignedMessageEnvelope {
  uint64_t sender_id;
  uint64_t target_actor_id;
  std::string message_type;
  std::string payload;
  std::promise<void>* ack_promise = nullptr;

  AlignedMessageEnvelope() = default;

  AlignedMessageEnvelope(uint64_t s, uint64_t t, std::string mt, std::string pl)
      : sender_id(s), target_actor_id(t), message_type(std::move(mt)), payload(std::move(pl)) {}

  // 支持移动
  AlignedMessageEnvelope(AlignedMessageEnvelope&& other) noexcept
      : sender_id(other.sender_id),
        target_actor_id(other.target_actor_id),
        message_type(std::move(other.message_type)),
        payload(std::move(other.payload)),
        ack_promise(other.ack_promise) {
    other.ack_promise = nullptr;
  }

  AlignedMessageEnvelope& operator=(AlignedMessageEnvelope&& other) noexcept {
    if (this != &other) {
      sender_id = other.sender_id;
      target_actor_id = other.target_actor_id;
      message_type = std::move(other.message_type);
      payload = std::move(other.payload);
      ack_promise = other.ack_promise;
      other.ack_promise = nullptr;
    }
    return *this;
  }

  // 禁用拷贝
  AlignedMessageEnvelope(const AlignedMessageEnvelope&) = delete;
  AlignedMessageEnvelope& operator=(const AlignedMessageEnvelope&) = delete;
};

// ==================== 批量消息处理器 ====================

template <size_t BatchSize = 64>
class BatchMessageHandler {
 public:
  using MessageHandler = std::function<void(const AlignedMessageEnvelope&)>;
  using BatchHandler = std::function<void(std::vector<AlignedMessageEnvelope>&)>;

  void RegisterHandler(const std::string& msg_type, MessageHandler handler) {
    single_handlers_[msg_type] = std::move(handler);
  }

  void RegisterBatchHandler(const std::string& msg_type, BatchHandler handler) {
    batch_handlers_[msg_type] = std::move(handler);
  }

  // 批量处理消息（高吞吐路径）
  template <typename Queue>
  size_t ProcessBatch(Queue& queue, size_t max_batch = BatchSize) {
    std::vector<AlignedMessageEnvelope> batch;
    batch.reserve(max_batch);

    // 批量出队
    size_t dequeued = 0;
    while (dequeued < max_batch) {
      auto msg = queue.dequeue();
      if (!msg) break;
      batch.push_back(std::move(*msg));
      dequeued++;
    }

    if (batch.empty()) return 0;

    // 按类型分组
    // ... (简化: 逐个处理但减少锁争用)
    for (auto& msg : batch) {
      auto it = single_handlers_.find(msg.message_type);
      if (it != single_handlers_.end()) {
        try {
          it->second(msg);
        } catch (const std::exception& e) {
          PE_LOG_ERROR("Batch handler error: {}", e.what());
        }
      }

      if (msg.ack_promise) {
        msg.ack_promise->set_value();
        delete msg.ack_promise;
      }
    }

    return dequeued;
  }

 private:
  std::unordered_map<std::string, MessageHandler> single_handlers_;
  std::unordered_map<std::string, BatchHandler> batch_handlers_;
};

// ==================== Lock-Free Actor ====================

class LockFreeActor {
 public:
  using MessageHandler = std::function<void(const AlignedMessageEnvelope&)>;

  explicit LockFreeActor(uint64_t actor_id) : actor_id_(actor_id), running_(false) {}

  virtual ~LockFreeActor() { Stop(); }

  uint64_t GetId() const { return actor_id_; }

  void Start() {
    running_ = true;
    worker_ = std::thread([this]() { RunLoop(); });
  }

  void Stop() {
    running_ = false;
    if (worker_.joinable()) {
      worker_.join();
    }
    Drain();
  }

  void Tell(uint64_t sender, std::string msg_type, std::string payload) {
    queue_.enqueue(
        AlignedMessageEnvelope(sender, actor_id_, std::move(msg_type), std::move(payload)));
  }

  size_t PendingMessages() const { return queue_.size_approx(); }

  void RegisterHandler(std::string msg_type, MessageHandler handler) {
    handlers_[std::move(msg_type)] = std::move(handler);
  }

 protected:
  virtual void OnReceive(const AlignedMessageEnvelope& msg) {
    auto it = handlers_.find(msg.message_type);
    if (it != handlers_.end()) {
      it->second(msg);
    }
  }

  // 批量处理版本
  virtual void OnReceiveBatch(std::vector<AlignedMessageEnvelope>& batch) {
    for (auto& msg : batch) OnReceive(msg);
  }

 private:
  void RunLoop() {
    constexpr size_t BATCH_SIZE = 64;
    std::vector<AlignedMessageEnvelope> batch;
    batch.reserve(BATCH_SIZE);

    while (running_) {
      batch.clear();

      // 批量出队
      size_t count = 0;
      while (count < BATCH_SIZE) {
        auto msg = queue_.dequeue();
        if (!msg) break;
        batch.push_back(std::move(*msg));
        count++;
      }

      if (batch.empty()) {
        // 短暂自旋等待
        for (int i = 0; i < 1000 && running_ && queue_.empty(); ++i) {
          __builtin_ia32_pause();  // CPU pause 指令
        }
        if (batch.empty()) {
          std::this_thread::sleep_for(std::chrono::microseconds(50));
          continue;
        }
      }

      // 批量处理
      for (auto& msg : batch) {
        try {
          OnReceive(msg);
        } catch (const std::exception& e) {
          PE_LOG_ERROR("Actor {} error processing '{}': {}", actor_id_, msg.message_type, e.what());
        }

        if (msg.ack_promise) {
          msg.ack_promise->set_value();
          delete msg.ack_promise;
        }
      }
    }

    Drain();
  }

  void Drain() {
    while (auto msg = queue_.dequeue()) {
      try {
        OnReceive(*msg);
      } catch (const std::exception& e) {
        PE_LOG_ERROR("Actor {} drain error: {}", actor_id_, e.what());
      }
      if (msg->ack_promise) {
        msg->ack_promise->set_value();
        delete msg->ack_promise;
      }
    }
  }

  uint64_t actor_id_;
  std::thread worker_;
  std::atomic<bool> running_;

  SPSCQueue<AlignedMessageEnvelope, 4096> queue_;  // 64K 消息缓冲

  std::unordered_map<std::string, MessageHandler> handlers_;
};

}  // namespace poker_engine::concurrency
