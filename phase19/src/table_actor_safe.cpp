// SafeHandler — wraps Actor callbacks to prevent silent exception swallowing
// Apply these patterns to table_actor.cpp and other Actor handlers

#include "poker_engine/base/logging.h"
#include "poker_engine/base/result.h"

namespace poker_engine::network {

// ==================== Exception-Safe Handler Wrapper ====================

class SafeHandler {
 public:
  // Wrap void-returning handler
  template <typename Func>
  static base::Result<void> Wrap(Func&& func) {
    try {
      func();
      return base::Result<void>::Ok();
    } catch (const std::exception& e) {
      PE_LOG_ERROR("Handler exception: {}", e.what());
      // AUDIT_LOG would go here in production
      return base::Result<void>::Err(base::MakeErrorCode(base::Error::InternalError));
    } catch (...) {
      PE_LOG_ERROR("Unknown exception in handler");
      return base::Result<void>::Err(base::MakeErrorCode(base::Error::InternalError));
    }
  }

  // Wrap value-returning handler
  template <typename T, typename Func>
  static base::Result<T> WrapWithResult(Func&& func) {
    try {
      return base::Result<T>::Ok(func());
    } catch (const std::exception& e) {
      PE_LOG_ERROR("Handler exception: {}", e.what());
      return base::Result<T>::Err(base::MakeErrorCode(base::Error::InternalError));
    } catch (...) {
      PE_LOG_ERROR("Unknown exception in handler");
      return base::Result<T>::Err(base::MakeErrorCode(base::Error::InternalError));
    }
  }
};

// ==================== Usage Pattern ====================
//
// BEFORE (dangerous — silent exception swallowing):
//
//   RegisterHandler("player_action", [this](const auto& msg) {
//       HandlePlayerAction(msg);  // exception kills actor silently
//   });
//
// AFTER (safe — exception captured, logged, error sent):
//
//   RegisterHandler("player_action", [this](const auto& msg) {
//       auto result = SafeHandler::Wrap([&]() {
//           HandlePlayerAction(msg);
//       });
//       if (!result.IsOk()) {
//           SendError(msg, 500, "Internal error");
//       }
//   });
//
// For value-returning handlers:
//
//   RegisterHandler("get_state", [this](const auto& msg) {
//       auto result = SafeHandler::WrapWithResult<GameState>([&]() {
//           return BuildGameStateSnapshot();
//       });
//       if (result.IsOk()) {
//           SendResponse(msg, result.Unwrap());
//       } else {
//           SendError(msg, 500, "State unavailable");
//       }
//   });

}  // namespace poker_engine::network
