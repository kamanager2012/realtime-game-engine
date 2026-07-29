#pragma once

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace poker_engine::persistence {

// ==================== 安全 SQL 查询构建器 ====================
// 使用参数化查询防止 SQL 注入

class SafeQuery {
 public:
  SafeQuery(std::string sql_template) : template_(std::move(sql_template)) {}

  // 绑定参数（自动转义）
  SafeQuery& Bind(int value) {
    params_.push_back(std::to_string(value));
    return *this;
  }

  SafeQuery& Bind(int64_t value) {
    params_.push_back(std::to_string(value));
    return *this;
  }

  SafeQuery& Bind(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    params_.push_back(oss.str());
    return *this;
  }

  SafeQuery& Bind(const std::string& value) {
    params_.push_back(Escape(value));
    return *this;
  }

  SafeQuery& Bind(const char* value) { return Bind(std::string(value)); }

  SafeQuery& Bind(bool value) {
    params_.push_back(value ? "1" : "0");
    return *this;
  }

  // 生成最终 SQL
  std::string Build() const {
    std::string result = template_;

    // 从后往前替换 ? 占位符（避免索引偏移）
    for (int i = static_cast<int>(params_.size()) - 1; i >= 0; --i) {
      size_t pos = FindNthPlaceholder(result, i + 1);
      if (pos != std::string::npos) {
        result.replace(pos, 1, params_[static_cast<size_t>(i)]);
      }
    }

    return result;
  }

  // 直接生成参数化查询（SQLite 预处理语句）
  const std::string& Template() const { return template_; }
  const std::vector<std::string>& Params() const { return params_; }

 private:
  std::string template_;
  std::vector<std::string> params_;

  // 查找第 n 个 ? 占位符
  static size_t FindNthPlaceholder(const std::string& sql, int n) {
    int count = 0;
    for (size_t i = 0; i < sql.size(); ++i) {
      // 跳过字符串字面量
      if (sql[i] == '\'') {
        i = sql.find('\'', i + 1);
        if (i == std::string::npos) break;
        continue;
      }
      if (sql[i] == '?') {
        count++;
        if (count == n) return i;
      }
    }
    return std::string::npos;
  }

  // 转义字符串值
  static std::string Escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);
    escaped += '\'';
    for (char c : value) {
      if (c == '\'')
        escaped += "''";
      else if (c == '\"')
        escaped += "\\\"";
      else if (c == '\\')
        escaped += "\\\\";
      else if (c == '\0')
        escaped += "\\0";
      else if (c == '\n')
        escaped += "\\n";
      else if (c == '\r')
        escaped += "\\r";
      else if (c == '\x1a')
        escaped += "\\Z";  // Ctrl+Z (Windows EOF)
      else
        escaped += c;
    }
    escaped += '\'';
    return escaped;
  }
};

// ==================== SQL 构建辅助 ====================

namespace sql {

// SELECT 构建器
class Select {
 public:
  Select(const std::string& columns) : columns_(columns) {}

  Select& From(const std::string& table) {
    from_ = table;
    return *this;
  }

  Select& Where(const std::string& condition) {
    where_ = condition;
    return *this;
  }

  Select& OrderBy(const std::string& order) {
    order_by_ = order;
    return *this;
  }

  Select& Limit(int n) {
    limit_ = n;
    return *this;
  }

  Select& Offset(int n) {
    offset_ = n;
    return *this;
  }

  std::string Build() const {
    std::ostringstream oss;
    oss << "SELECT " << columns_ << " FROM " << from_;
    if (!where_.empty()) oss << " WHERE " << where_;
    if (!order_by_.empty()) oss << " ORDER BY " << order_by_;
    if (limit_ >= 0) oss << " LIMIT " << limit_;
    if (offset_ >= 0) oss << " OFFSET " << offset_;
    return oss.str();
  }

 private:
  std::string columns_;
  std::string from_;
  std::string where_;
  std::string order_by_;
  int limit_ = -1;
  int offset_ = -1;
};

// INSERT 构建器
class Insert {
 public:
  Insert(const std::string& table) : table_(table) {}

  Insert& Columns(const std::vector<std::string>& cols) {
    columns_ = cols;
    return *this;
  }

  Insert& Values(const std::vector<std::string>& vals) {
    values_ = vals;
    return *this;
  }

  std::string Build() const {
    std::ostringstream oss;
    oss << "INSERT INTO " << table_ << " (";
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << columns_[i];
    }
    oss << ") VALUES (";
    for (size_t i = 0; i < values_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << values_[i];
    }
    oss << ")";
    return oss.str();
  }

 private:
  std::string table_;
  std::vector<std::string> columns_;
  std::vector<std::string> values_;
};

// UPDATE 构建器
class Update {
 public:
  Update(const std::string& table) : table_(table) {}

  Update& Set(const std::string& column, const std::string& value) {
    sets_.push_back(column + " = " + value);
    return *this;
  }

  Update& Where(const std::string& condition) {
    where_ = condition;
    return *this;
  }

  std::string Build() const {
    std::ostringstream oss;
    oss << "UPDATE " << table_ << " SET ";
    for (size_t i = 0; i < sets_.size(); ++i) {
      if (i > 0) oss << ", ";
      oss << sets_[i];
    }
    if (!where_.empty()) oss << " WHERE " << where_;
    return oss.str();
  }

 private:
  std::string table_;
  std::vector<std::string> sets_;
  std::string where_;
};

}  // namespace sql

}  // namespace poker_engine::persistence
