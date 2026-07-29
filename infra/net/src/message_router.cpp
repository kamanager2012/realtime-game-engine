#include "poker_engine/network/message_router.h"

namespace poker_engine::network {

void MessageRouter::Register(int op, Handler handler) { handlers_[op] = std::move(handler); }

void MessageRouter::Unregister(int op) { handlers_.erase(op); }

bool MessageRouter::Route(const Message& msg) const {
  auto it = handlers_.find(msg.op);
  if (it != handlers_.end()) {
    it->second(msg);
    return true;
  }
  return false;
}

bool MessageRouter::HasHandler(int op) const { return handlers_.count(op) > 0; }

}  // namespace poker_engine::network
