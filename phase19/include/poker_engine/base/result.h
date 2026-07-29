#pragma once

#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <variant>

namespace poker_engine::base {

// ==================== Result<T, E> — 受 Rust 启发 ====================

template <typename T, typename E = std::error_code>
class Result {
 public:
  // ===== 构造 =====

  Result()
    requires std::is_default_constructible_v<T>
      : ok_(true), data_{} {}

  explicit Result(const T& v) : ok_(true), data_{v} {}
  explicit Result(T&& v) : ok_(true), data_{std::move(v)} {}

  explicit Result(const E& e) : ok_(false), data_{e} {}
  explicit Result(E&& e) : ok_(false), data_{std::move(e)} {}

  // 静态工厂
  static Result Ok(T value) { return Result(std::move(value)); }
  static Result Err(E error) { return Result(std::move(error)); }

  // ===== 查询 =====

  bool IsOk() const { return ok_; }
  bool IsErr() const { return !ok_; }

  explicit operator bool() const { return ok_; }

  // ===== 访问 =====

  // 安全访问：带兜底值
  const T& UnwrapOr(const T& fallback) const { return ok_ ? std::get<T>(data_) : fallback; }

  T UnwrapOr(T&& fallback) const {
    return ok_ ? std::move(std::get<T>(data_)) : std::move(fallback);
  }

  // 不安全访问：ok_ = true 时有效
  const T& Unwrap() const {
    if (!ok_) throw std::runtime_error("Result: unwrap on Err");
    return std::get<T>(data_);
  }

  T& Unwrap() {
    if (!ok_) throw std::runtime_error("Result: unwrap on Err");
    return std::get<T>(data_);
  }

  T* operator->() {
    if (!ok_) throw std::runtime_error("Result: operator-> on Err");
    return &std::get<T>(data_);
  }

  const T* operator->() const {
    if (!ok_) throw std::runtime_error("Result: operator-> on Err");
    return &std::get<T>(data_);
  }

  T& operator*() {
    if (!ok_) throw std::runtime_error("Result: operator* on Err");
    return std::get<T>(data_);
  }

  const T& operator*() const {
    if (!ok_) throw std::runtime_error("Result: operator* on Err");
    return std::get<T>(data_);
  }

  const E& Error() const {
    if (ok_) throw std::runtime_error("Result: error on Ok");
    return std::get<E>(data_);
  }

  // ===== 变换 =====

  template <typename F>
  auto Map(F&& f) const -> Result<decltype(f(std::declval<T>())), E> {
    using U = decltype(f(std::declval<T>()));
    if (!ok_) return Result<U, E>::Err(std::get<E>(data_));
    return Result<U, E>::Ok(f(std::get<T>(data_)));
  }

  template <typename F>
  auto AndThen(F&& f) const -> decltype(f(std::declval<T>())) {
    if (!ok_) return typename decltype(f(std::declval<T>()))::Err(std::get<E>(data_));
    return f(std::get<T>(data_));
  }

  template <typename F>
  Result<T, E> Inspect(F&& f) const {
    if (ok_) f(std::get<T>(data_));
    return *this;
  }

  template <typename F>
  Result<T, E> InspectErr(F&& f) const {
    if (!ok_) f(std::get<E>(data_));
    return *this;
  }

 private:
  bool ok_;
  std::variant<T, E> data_;
};

// 特化 void 版本
template <typename E>
class Result<void, E> {
 public:
  Result() : ok_(true) {}

  explicit Result(const E& e) : ok_(false), error_(e) {}
  explicit Result(E&& e) : ok_(false), error_(std::move(e)) {}

  static Result Ok() { return Result(); }
  static Result Err(E error) { return Result(std::move(error)); }

  bool IsOk() const { return ok_; }
  bool IsErr() const { return !ok_; }
  explicit operator bool() const { return ok_; }

  const E& Error() const {
    if (ok_) throw std::runtime_error("Result: error on Ok");
    return error_;
  }

 private:
  bool ok_;
  E error_{};
};

// ==================== 预定义错误 ====================

enum class Error {
  Ok = 0,
  NullPointer,
  OutOfRange,
  InvalidArgument,
  NotFound,
  AlreadyExists,
  PermissionDenied,
  Timeout,
  IoError,
  ParseError,
  DatabaseError,
  NetworkError,
  AuthenticationFailed,
  RateLimited,
  InternalError,
  NotConnected,
  OperationNotSupported,
};

const char* ErrorToString(Error e);

// 错误类别
class PokerErrorCategory : public std::error_category {
 public:
  const char* name() const noexcept override { return "poker-engine"; }
  std::string message(int ev) const override;
};

std::error_code MakeErrorCode(Error e);

}  // namespace poker_engine::base

// 为 std::is_error_code_enum 提供特化
namespace std {
template <>
struct is_error_code_enum<poker_engine::base::Error> : true_type {};
}  // namespace std
