#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace poker_engine::network {

// ==================== 使用 Owned/Shared 替代裸指针 ====================

class TableStateNode : public std::enable_shared_from_this<TableStateNode> {
 public:
  using Ptr = std::shared_ptr<TableStateNode>;
  using ConstPtr = std::shared_ptr<const TableStateNode>;

  // Immutable view (thread-safe)
  class ImmutableView {
   public:
    explicit ImmutableView(ConstPtr node) : node_(std::move(node)) {}

    // Accessors would go here based on game::GameState
    int64_t GetVersion() const { return node_->version_; }

   private:
    ConstPtr node_;
  };

  // Construct with initial state
  explicit TableStateNode(int64_t version = 0) : version_(version) {}

  // Copy-on-Write: create next version
  Ptr NextVersion() const { return std::make_shared<TableStateNode>(version_ + 1); }

  ImmutableView View() const { return ImmutableView(shared_from_this()); }

  int64_t Version() const { return version_; }

 private:
  int64_t version_;
};

// ==================== 带智能指针的消息 ====================

struct TableMessage {
  using Ptr = std::shared_ptr<TableMessage>;

  enum class Type { JoinTable, LeaveTable, PlayerAction, GetState, BroadcastState };

  Type type;
  int64_t player_id;
  std::string payload;

  static Ptr Create(Type t, int64_t pid, std::string payload) {
    auto msg = std::make_shared<TableMessage>();
    msg->type = t;
    msg->player_id = pid;
    msg->payload = std::move(payload);
    return msg;
  }
};

}  // namespace poker_engine::network
