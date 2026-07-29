#include "poker_engine/phase14/query_builder.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace poker_engine::phase14 {

QueryBuilder::QueryBuilder(const std::string& table) : table_(table) {}

std::string QueryBuilder::WrapAggregate(const std::string& func, const std::string& col,
                                        const std::string& alias) const {
  std::string result = func + "(" + (col.empty() ? "*" : col) + ")";
  if (!alias.empty()) result += " AS " + alias;
  return result;
}

QueryBuilder& QueryBuilder::Select(const std::string& columns) {
  select_ = columns;
  return *this;
}

QueryBuilder& QueryBuilder::Where(const std::string& condition) {
  where_clauses_.push_back(condition);
  return *this;
}

QueryBuilder& QueryBuilder::WhereEq(const std::string& column, const std::string& value) {
  return Where(column + " = '" + value + "'");
}

QueryBuilder& QueryBuilder::WhereGt(const std::string& column, double value) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s > %.6f", column.c_str(), value);
  return Where(buf);
}

QueryBuilder& QueryBuilder::WhereBetween(const std::string& column, double min_val,
                                         double max_val) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s BETWEEN %.6f AND %.6f", column.c_str(), min_val, max_val);
  return Where(buf);
}

QueryBuilder& QueryBuilder::WhereIn(const std::string& column, const std::vector<int>& values) {
  std::string in_str;
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) in_str += ",";
    in_str += std::to_string(values[i]);
  }
  return Where(column + " IN (" + in_str + ")");
}

QueryBuilder& QueryBuilder::WhereLike(const std::string& column, const std::string& pattern) {
  return Where(column + " LIKE '%" + pattern + "%'");
}

QueryBuilder& QueryBuilder::GroupBy(const std::string& columns) {
  group_by_ = columns;
  return *this;
}

QueryBuilder& QueryBuilder::Having(const std::string& condition) {
  having_ = condition;
  return *this;
}

QueryBuilder& QueryBuilder::OrderBy(const std::string& column, bool ascending) {
  order_by_ = column + (ascending ? " ASC" : " DESC");
  return *this;
}

QueryBuilder& QueryBuilder::OrderByDesc(const std::string& column) {
  return OrderBy(column, false);
}

QueryBuilder& QueryBuilder::Limit(int count) {
  limit_ = count;
  return *this;
}

QueryBuilder& QueryBuilder::Offset(int count) {
  offset_ = count;
  return *this;
}

QueryBuilder& QueryBuilder::Join(const std::string& table, const std::string& on_condition) {
  joins_.push_back({"JOIN " + table, on_condition});
  return *this;
}

QueryBuilder& QueryBuilder::LeftJoin(const std::string& table, const std::string& on_condition) {
  joins_.push_back({"LEFT JOIN " + table, on_condition});
  return *this;
}

QueryBuilder& QueryBuilder::Count(const std::string& alias) {
  aggregates_.push_back(WrapAggregate("COUNT", "*", alias.empty() ? "count" : alias));
  return *this;
}

QueryBuilder& QueryBuilder::Sum(const std::string& column, const std::string& alias) {
  std::string a = alias.empty() ? ("sum_" + column) : alias;
  aggregates_.push_back(WrapAggregate("SUM", column, a));
  return *this;
}

QueryBuilder& QueryBuilder::Avg(const std::string& column, const std::string& alias) {
  std::string a = alias.empty() ? ("avg_" + column) : alias;
  aggregates_.push_back(WrapAggregate("AVG", column, a));
  return *this;
}

QueryBuilder& QueryBuilder::Min(const std::string& column, const std::string& alias) {
  std::string a = alias.empty() ? ("min_" + column) : alias;
  aggregates_.push_back(WrapAggregate("MIN", column, a));
  return *this;
}

QueryBuilder& QueryBuilder::Max(const std::string& column, const std::string& alias) {
  std::string a = alias.empty() ? ("max_" + column) : alias;
  aggregates_.push_back(WrapAggregate("MAX", column, a));
  return *this;
}

std::string QueryBuilder::Build() const {
  std::ostringstream oss;

  // SELECT
  oss << "SELECT ";
  if (!aggregates_.empty()) {
    for (size_t i = 0; i < aggregates_.size(); i++) {
      if (i > 0) oss << ", ";
      oss << aggregates_[i];
    }
  } else {
    oss << select_;
  }

  // FROM
  oss << " FROM " << table_;

  // JOINs
  for (const auto& [join_table, on] : joins_) {
    oss << " " << join_table << " ON " << on;
  }

  // WHERE
  if (!where_clauses_.empty()) {
    oss << " WHERE ";
    for (size_t i = 0; i < where_clauses_.size(); i++) {
      if (i > 0) oss << " AND ";
      oss << "(" << where_clauses_[i] << ")";
    }
  }

  // GROUP BY
  if (!group_by_.empty()) {
    oss << " GROUP BY " << group_by_;
  }

  // HAVING
  if (!having_.empty()) {
    oss << " HAVING " << having_;
  }

  // ORDER BY
  if (!order_by_.empty()) {
    oss << " ORDER BY " << order_by_;
  }

  // LIMIT
  if (limit_ >= 0) {
    oss << " LIMIT " << limit_;
  }

  // OFFSET
  if (offset_ >= 0) {
    oss << " OFFSET " << offset_;
  }

  return oss.str();
}

std::string QueryBuilder::BuildCount() const {
  QueryBuilder copy = *this;
  copy.aggregates_.clear();
  copy.select_ = "COUNT(*)";
  copy.order_by_ = "";
  copy.limit_ = -1;
  copy.offset_ = -1;
  return copy.Build();
}

}  // namespace poker_engine::phase14
