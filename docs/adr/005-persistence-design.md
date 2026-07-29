# ADR-005 数据持久化设计

| 字段         | 值                                          |
|-------------|---------------------------------------------|
| 编号        | 005                                         |
| 标题        | 数据库持久化方案设计                          |
| 状态        | 📝 Draft                                     |
| 日期        | 2026-06-15                                   |
| 决策者      | James                                       |

---

## 1. 上下文

phase14 已有 repository 层的接口定义（`player_repository.h`, `hand_repository.h`, `stat_repository.h`, `database_manager.h`, `query_builder.h`, `migration.h`），但未集成到运行时。需要确定数据存储方案、表结构和集成方式。

## 2. 存储引擎选型

| 选项 | 优点 | 缺点 | 推荐场景 |
|------|------|------|----------|
| **SQLite** | 零配置、嵌入式、单文件 | 并发写入受限 | MVP / 开发 / 小规模 |
| **PostgreSQL** | 强并发、JSONB、扩展性好 | 需要独立服务 | 生产环境 |

**决策**：MVP 用 SQLite，架构设计兼容 PostgreSQL 切换。通过 `DatabaseManager` 抽象层隔离差异。

## 3. 数据库 Schema

### 3.1 表结构

```sql
-- 玩家表
CREATE TABLE players (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT UNIQUE NOT NULL,
    display_name TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    chips       REAL NOT NULL DEFAULT 1000.00,
    total_profit REAL NOT NULL DEFAULT 0.0,
    hands_played INTEGER NOT NULL DEFAULT 0,
    elo_rating  INTEGER NOT NULL DEFAULT 1500
);

-- 手牌历史表
CREATE TABLE hand_histories (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    table_id        TEXT NOT NULL,
    hand_number     INTEGER NOT NULL,
    start_time      TEXT NOT NULL DEFAULT (datetime('now')),
    end_time        TEXT,
    phase           TEXT NOT NULL,
    community_cards TEXT,
    pot_amount      REAL NOT NULL DEFAULT 0,
    winner_ids      TEXT,
    raw_actions     TEXT NOT NULL
);

-- 玩家手牌关联（多对多）
CREATE TABLE hand_players (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    hand_id         INTEGER NOT NULL REFERENCES hand_histories(id),
    player_id       INTEGER NOT NULL REFERENCES players(id),
    seat_index      INTEGER NOT NULL,
    hole_cards      TEXT,
    hole_cards_encrypted TEXT,
    starting_chips  REAL NOT NULL,
    ending_chips    REAL NOT NULL,
    net_profit      REAL NOT NULL,
    final_hand_rank TEXT,
    is_winner       BOOLEAN NOT NULL DEFAULT 0
);

-- 行动记录
CREATE TABLE action_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    hand_id         INTEGER NOT NULL REFERENCES hand_histories(id),
    player_id       INTEGER NOT NULL REFERENCES players(id),
    round           TEXT NOT NULL,
    action_type     TEXT NOT NULL,
    amount          REAL NOT NULL,
    acted_at       TEXT NOT NULL DEFAULT (datetime('now'))
);

-- 统计摘要（预计算加速查询）
CREATE TABLE player_stats (
    player_id       INTEGER PRIMARY KEY REFERENCES players(id),
    total_hands     INTEGER NOT NULL DEFAULT 0,
    hands_won       INTEGER NOT NULL DEFAULT 0,
    total_profit    REAL NOT NULL DEFAULT 0,
    vpip_pct        REAL NOT NULL DEFAULT 0,
    pfr_pct         REAL NOT NULL DEFAULT 0,
    aggr_factor     REAL NOT NULL DEFAULT 0,
    win_rate        REAL NOT NULL DEFAULT 0,
    last_updated    TEXT NOT NULL DEFAULT (datetime('now'))
);

-- 索引
CREATE INDEX idx_hand_histories_table ON hand_histories(table_id, hand_number);
CREATE INDEX idx_hand_players_player ON hand_players(player_id);
CREATE INDEX idx_action_log_hand ON action_log(hand_id);
CREATE INDEX idx_action_log_player_round ON action_log(player_id, round);
```

### 3.2 数据流

```
一手牌结束
    │
    ▼
ShowdownEvaluator 结算
    │
    ▼
Table::OnHandComplete()
    │
    ├──▶ HandRepository::Save(hand_history)  → hand_histories + hand_players + action_log
    │
    ├──▶ PlayerRepository::UpdateStats(player_id)  → 更新 player_stats
    │
    └──▶ (可选) 触发 AI 训练数据导出
              └──▶ 写入训练数据文件 / 推送到在线学习队列
```

## 4. Repository 接口契约

```cpp
class IHandRepository {
public:
    virtual ~IHandRepository() = default;

    virtual int64_t SaveHand(const HandHistory& hand) = 0;
    virtual std::optional<HandHistory> GetHand(int64_t hand_id) = 0;
    virtual std::vector<HandHistory> GetPlayerHands(int64_t player_id, int limit = 100) = 0;
    virtual std::vector<HandHistory> GetTableHands(const std::string& table_id, int limit = 100) = 0;
};

class IPlayerRepository {
public:
    virtual ~IPlayerRepository() = default;

    virtual int64_t CreatePlayer(const std::string& username, const std::string& display_name) = 0;
    virtual std::optional<Player> GetPlayer(int64_t player_id) = 0;
    virtual std::optional<Player> GetPlayerByUsername(const std::string& username) = 0;
    virtual void UpdatePlayerStats(int64_t player_id, const PlayerStats& stats) = 0;
    virtual std::vector<Player> GetLeaderboard(int limit = 100) = 0;
};

class IActionLogRepository {
public:
    virtual ~IActionLogRepository() = default;

    virtual void LogAction(int64_t hand_id, const ActionRecord& action) = 0;
    virtual std::vector<ActionRecord> GetActions(int64_t hand_id) = 0;
};
```

## 5. 迁移策略

使用 `migration.h` 实现版本化迁移：

```
V001__create_players.sql
V002__create_hand_histories.sql
V003__create_action_log.sql
V004__add_elo_to_players.sql
V005__add_encryption_fields.sql
```

启动时自动检测 schema 版本，执行增量迁移。

## 6. 加密与隐私

| 数据 | 存储方式 | 说明 |
|------|---------|------|
| 手牌（发给玩家的底牌）| 翻牌前加密存储 | 仅摊牌后解密写入 `hole_cards` |
| 玩家密码/Token | bcrypt 哈希 | 永不存储明文 |
| 统计数据 | 明文 | 非敏感数据 |

## 7. 关联 ADR

- ADR-001: Persistence Layer Component 部分
- ADR-002: Game Server 调用 Repository 保存手牌
