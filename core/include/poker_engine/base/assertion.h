#pragma once
#include <cassert>
#include <cstdlib>
#include <functional>
#include <string>

#include "poker_engine/base/logging.h"

namespace poker_engine {

// ==================== 可配置断言处理 ====================
//
// Abort 模式：std::abort()（dev/test 安全网）
// Report 模式：log + handler，返回 false 让调用方决定后续行为
//
// 用法：
//   PE_ASSERT(ptr != nullptr);          // 语句形式，Abort 下崩溃，Report 下继续
//   if (PE_ASSERT_RET(x > 0)) {         // 返回 bool，可做条件判断
//       // safe path
//   } else {
//       return Error::InvalidInput;      // 生产环境优雅降级
//   }

enum class AssertBehavior {
  Abort,  // 开发/测试：立即终止
  Report  // 生产：记录错误，返回 false
};

using AssertHandler =
    std::function<void(const std::string& msg, const std::string& file, int line)>;

class AssertConfig {
 public:
  static AssertConfig& Instance() {
    static AssertConfig instance;
    return instance;
  }

  void SetBehavior(AssertBehavior behavior) { behavior_ = behavior; }
  AssertBehavior GetBehavior() const { return behavior_; }

  void SetCustomHandler(AssertHandler handler) { custom_handler_ = std::move(handler); }
  const AssertHandler& GetCustomHandler() const { return custom_handler_; }

 private:
  AssertConfig() = default;
  AssertBehavior behavior_ = AssertBehavior::Abort;
  AssertHandler custom_handler_;
};

// 内部使用：统一断言失败入口，返回 false（Report 模式）或 abort（Abort 模式）
inline bool AssertFailed(const std::string& msg, const std::string& file, int line) {
  const auto& config = AssertConfig::Instance();
  std::string full_msg =
      std::string("Assertion failed: ") + msg + " at " + file + ":" + std::to_string(line);
  Logger::Instance().Error(full_msg);

  if (config.GetCustomHandler()) {
    config.GetCustomHandler()(full_msg, file, line);
  }

  if (config.GetBehavior() == AssertBehavior::Abort) {
    std::abort();
  }
  return false;  // Report 模式：返回 false 让调用方处理
}

// 语句形式 — 独立使用，不做条件判断
#define PE_ASSERT(cond)                                                 \
  do {                                                                  \
    if (!(cond)) poker_engine::AssertFailed(#cond, __FILE__, __LINE__); \
  } while (0)

// 带消息的语句形式
#define PE_ASSERT_MSG(cond, msg)                                                               \
  do {                                                                                         \
    if (!(cond))                                                                               \
      poker_engine::AssertFailed(std::string(msg) + " [cond: " #cond "]", __FILE__, __LINE__); \
  } while (0)

// 返回 bool 形式 — 可用于条件分支
// if (PE_ASSERT_RET(ptr != nullptr)) { use(ptr); } else { return Error; }
#define PE_ASSERT_RET(cond) ((cond) ? true : poker_engine::AssertFailed(#cond, __FILE__, __LINE__))

}  // namespace poker_engine
