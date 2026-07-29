#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace poker_engine::phase14 {

// ========== SQL查询构建器 (链式API) ==========

class QueryBuilder {
 public:
  explicit QueryBuilder(const std::string& table);

  // SELECT
  QueryBuilder& Select(const std::string& columns = "*");

  // WHERE
  QueryBuilder& Where(const std::string& condition);
  QueryBuilder& WhereEq(const std::string& column, const std::string& value);
  QueryBuilder& WhereGt(const std::string& column, double value);
  QueryBuilder& WhereBetween(const std::string& column, double min_val, double max_val);
  QueryBuilder& WhereIn(const std::string& column, const std::vector<int>& values);
  QueryBuilder& WhereLike(const std::string& column, const std::string& pattern);

  // GROUP BY / HAVING
  QueryBuilder& GroupBy(const std::string& columns);
  QueryBuilder& Having(const std::string& condition);

  // ORDER BY
  QueryBuilder& OrderBy(const std::string& column, bool ascending = true);
  QueryBuilder& OrderByDesc(const std::string& column);

  // LIMIT / OFFSET
  QueryBuilder& Limit(int count);
  QueryBuilder& Offset(int count);

  // JOIN
  QueryBuilder& Join(const std::string& table, const std::string& on_condition);
  QueryBuilder& LeftJoin(const std::string& table, const std::string& on_condition);

  // Aggregate
  QueryBuilder& Count(const std::string& alias = "count");
  QueryBuilder& Sum(const std::string& column, const std::string& alias = "");
  QueryBuilder& Avg(const std::string& column, const std::string& alias = "");
  QueryBuilder& Min(const std::string& column, const std::string& alias = "");
  QueryBuilder& Max(const std::string& column, const std::string& alias = "");

  // Building
  std::string Build() const;
  std::string BuildCount() const;

 private:
  std::string table_;
  std::string select_ = "*";
  std::vector<std::string> where_clauses_;
  std::string group_by_;
  std::string having_;
  std::string order_by_;
  int limit_ = -1;
  int offset_ = -1;
  std::vector<std::pair<std::string, std::string>> joins_;
  std::vector<std::string> aggregates_;

  std::string WrapAggregate(const std::string& func, const std::string& col,
                            const std::string& alias) const;
};

}  // namespace poker_engine::phase14
