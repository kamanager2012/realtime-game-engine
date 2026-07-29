#include "poker_engine/base/result.h"

namespace poker_engine::base {

const char* ErrorToString(Error e) {
  switch (e) {
    case Error::Ok:
      return "OK";
    case Error::NullPointer:
      return "Null pointer";
    case Error::OutOfRange:
      return "Out of range";
    case Error::InvalidArgument:
      return "Invalid argument";
    case Error::NotFound:
      return "Not found";
    case Error::AlreadyExists:
      return "Already exists";
    case Error::PermissionDenied:
      return "Permission denied";
    case Error::Timeout:
      return "Timeout";
    case Error::IoError:
      return "I/O error";
    case Error::ParseError:
      return "Parse error";
    case Error::DatabaseError:
      return "Database error";
    case Error::NetworkError:
      return "Network error";
    case Error::AuthenticationFailed:
      return "Authentication failed";
    case Error::RateLimited:
      return "Rate limited";
    case Error::InternalError:
      return "Internal error";
  }
  return "Unknown error";
}

std::error_code MakeErrorCode(Error e) { return {static_cast<int>(e), PokerErrorCategory{}}; }

std::string PokerErrorCategory::message(int ev) const {
  return ErrorToString(static_cast<Error>(ev));
}

}  // namespace poker_engine::base
