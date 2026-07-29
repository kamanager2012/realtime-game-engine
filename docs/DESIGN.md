# Poker Engine — 系统设计文档 v1.5

> 最后更新: 2026-06-22
> 作者: James Oldman
> 许可证: MIT

---

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [核心模块设计](#3-核心模块设计)
4. [游戏引擎设计](#4-游戏引擎设计)
5. [AI 分析引擎设计](#5-ai-分析引擎设计)
6. [网络层设计](#6-网络层设计)
7. [数据库设计](#7-数据库设计)
8. [CLI 工具设计](#8-cli-工具设计)
9. [Web 前端设计](#9-web-前端设计)
10. [通信协议](#10-通信协议)
11. [构建与部署](#11-构建与部署)
12. [开发指南](#12-开发指南)
13. [API 参考](#13-api-参考)
14. [演进路线](#14-演进路线)

---

## 1. 项目概述

### 1.1 愿景

构建一套**生产级德州扑克平台**，同时支持：
- **人机对战** — 本地 CLI + WebSocket 多人游戏
- **AI 研究** — 多种扑克 AI 算法的实现与对比
- **数据分析** — 玩家统计、方差分析、策略可视化
- **锦标赛模拟** — AI 自动化锦标赛与锦标赛结构设计

### 1.2 核心指标

| 指标 | 当前值 |
|------|--------|
| 总代码量 | ~62,000+ 行 C++ |
| 测试覆盖 | 400 单元测试 |
| CLI 工具 | 14 个 |
| 模块数 | 22 个 (phase1-21 + core + equity) |
| 支持游戏 | No-Limit Texas Hold'em |
| 最大玩家数 | 10 人/桌 |
| 游戏变体 | Cash Game + 锦标赛模拟 |

### 1.3 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++20 |
| 构建 | CMake 3.16+ (依赖 vendored，无需网络) |
| 测试 | Google Test |
| 数据库 | SQLite3 (WAL 模式) |
| 网络 | POSIX Socket + WebSocket (自实现) |
| 加密 | OpenSSL (SHA-1, Base64) |
| 并行 | OpenMP + 标准线程池 |
| Python 绑定 | pybind11 (Phase 2+) |
| 可选 | Boost.Asio (替代网络层) |

---

## 2. 系统架构

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────────┐
│                    表现层 (Presentation)                  │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ CLI 工具 │  │ WebSocket    │  │ Web 前端 (HTML)  │   │
│  │ 14个     │  │ 服务器       │  │ 3D 牌桌界面      │   │
│  └──────────┘  └──────────────┘  └──────────────────┘   │
├─────────────────────────────────────────────────────────┤
│                    业务逻辑层 (Logic)                     │
│  ┌──────────────────────┐  ┌──────────────────────────┐  │
│  │ 游戏引擎 (phase12)    │  │ AI 分析引擎 (phase1-11)  │  │
│  │ 状态机/底池/结算/发牌  │  │ CFR/MC-CFR/LUT/统计     │  │
│  └──────────────────────┘  └──────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │ 网络层 (phase13)                                     │  │
│  │ WebSocket 服务器 + AI 自动牌桌 + 客户端路由          │  │
│  └────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                    数据访问层 (Data)                      │
│  ┌────────────────────────────────────────────────────┐  │
│  │ 数据库层 (phase14)                                   │  │
│  │ SQLite3 │ 迁移管理 │ 手牌仓库 │ 玩家仓库 │ 统计仓库  │  │
│  └────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│                    基础设施层 (Foundation)                │
│  ┌────────────┐ ┌───────────┐ ┌──────────────────────┐  │
│  │ 核心 (core) │ │ 评估器     │ │ 工具库               │  │
│  │ Card/Deck  │ │ Equity    │ │ 并行/线程池/计时器     │  │
│  │ HandRank   │ │ Evaluator │ │ Profiler/Stopwatch   │  │
│  └────────────┘ └───────────┘ └──────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### 2.2 模块依赖图

```
core ← equity ← phase1 ← phase3 ← phase5 ← phase7 ← phase8
                ↑          ↑         ↑         ↑         ↑
                phase2     phase4    phase6    phase9    phase11
                                                     ↑    ↑
                                                   phase10 phase12
                                                        ↑      ↑
                                                 phase13    phase14
```

- `core`: 依赖 PokerHandEvaluator (vendored)，仅标准库
- `equity`: 依赖 core
- `phase N`: 依赖编号小于 N 的所有相关模块
- `phase12` (游戏引擎): 最终消费者
- `phase13` (网络): 依赖 phase12 + phase11
- `phase14` (数据库): 依赖 phase12 + phase13
- `phase15` (CFR扩展): CFR引擎 + 锦标赛管理
- `phase16` (安全): 回放引擎 + 反作弊 + 管理接口
- `phase17` (观战): 观战管理 + ML引擎 + 直播
- `phase18` (优化): 超参调优 + 贝叶斯优化 + 负载均衡
- `phase19` (评估集成): 独立手牌评估器 + 审计日志 + 限速
- `phase20` (CFR扩展2): FlatBuffer序列化 + CFR节点 + 策略网络
- `phase21` (加密安全): 加密工具 + GDPR + IP信誉 + mTLS

> **注意**: phase16-21 全部源码已在根 CMakeLists.txt 中启用，全部编译通过

### 2.3 命名空间结构

```
poker_engine::
├── core           — Card, Deck, HandRank, Serializer
├── equity         — EquityCalculator, Enumerator, MonteCarlo
├── evaluator      — HandEval (5/7张牌评估)
├── range          — Range (手牌范围), Combo
├── phase1         — HandHistoryParser, EVCalculator
├── phase2         — RangeBuilder, CFRSolver, Visualizer
├── phase3         — FlopExplorer, SpotSolver, Replay
├── phase4         — HHParser (完整), SessionAnalyzer
├── phase5         — BulkParser, EquityMatrix, ICM, Regression
├── phase6         — ICFR, RangeTracker, SQLiteDB, REST API
├── phase7         — CFRPlus, TournamentICM, OpponentModel
├── phase8         — MCCRDeep, PreflopGTO, ExploitEngine
├── phase9         — VarianceEngine, StrategyDiff, PolarizedRange
├── phase10        — ParallelUtils, ParallelCFR, PreflopLUT
├── phase11        — SolverManager, DecisionEngine, EngineRegistry
├── game           — GameState, Table, Player, Pot, Actions
├── network        — WebSocketServer, AIEngine
├── phase14        — Database, Migration, Repositories, QueryBuilder
├── phase15        — CFREngine, CFRAbstraction, TournamentManager
├── phase16        — ReplayEngine, Anticheat, AdminHandler
├── phase17        — SpectatorManager, MLEngine, TournamentBroadcaster
├── phase18        — HyperparamTuner, BayesianOptimizer, LoadBalancer
├── phase19        — HandEvaluator (standalone), AuditLogger, RateLimiter
├── phase20        — FlatBuffersSerializer, CFRNode, PolicyNetwork
└── phase21        — CryptoUtils, GDPR, IPReputation, MTLS, SecurityPolicy
```

---

## 3. 核心模块设计

### 3.1 卡牌表示 (core)

```
Card
├── 用一个 uint8_t 编码 (0-51)
│   高2位 = 花色 (s=0, h=1, d=2, c=3)
│   低3位 = 点数 (0=2, ..., 12=A)
├── 编码: card_id = rank * 4 + suit
├── 方法: Rank(), Suit(), ToString(), IsValid()
└── 性能: 内联，零开销抽象
```

**Deck 实现:**
- Fisher-Yates 洗牌，使用 `std::mt19937` 随机数引擎
- 支持从二进制文件序列化/反序列化牌堆状态
- 可验证的确定性洗牌（给定相同种子产生相同序列）

### 3.2 手牌评估 (evaluator)

```
EvalResult
├── 使用 PokerHandEvaluator (pheval) 算法
├── O(1) 时间评估任意 5/7 张牌
├── 位编码 value() 实现全序比较:
├──   category(4bit) | rank[0](4bit) | ... | rank[4](4bit)
│   高位 = 手牌类型，低位 = 踢脚牌排序
├── 所有比较运算符 (<, >, ==) 基于 value()，不依赖 standard_rank
└── standard_rank 字段保留兼容（默认 7462，当前未计算）

EquityCalculator
├── 精确穷举法 (≤6 手牌)
├── Monte Carlo 采样法 (大数据集)
└── multi-way 支持 (最多10方底池)
```

### 3.3 范围表示 (range)

```
Range
├── 1326 个可能的 Texas Hold'em 手牌组合
├── 使用紧凑位图存储 (uint16_t × 1326)
│   每个权重用 float 表示 (0.0-1.0)
├── 操作: Set/Get, Combine, Intersect, Exclude, Merge
├── 构造: Range::FromString("AKs,QQ+,AKo")
│   ├── 解析器支持: 
│   │   - 单手牌: "AKs", "72o", "AA"
│   │   - 范围: "77+", "A2s+", "ATo+"
│   │   - 逗号分隔组合
├── 性能: ~50ns/手牌查询
└── 序列化为紧凑二进制 (2KB)
```

**支持的Range语法:**
```
AA           — 仅口袋AA
77+          — 77到AA的所有对子
AKs          — 同花AK
A2s+         — 所有同花Ax牌
KTo+         — K带10+踢脚的所有杂色牌
22+,A2s+     — 组合多个范围 (逗号分隔)
```

---

## 4. 游戏引擎设计

### 4.1 状态机 (GameState)

游戏状态机管理一手牌的完整生命周期：

```
状态转换图:

WAITING ──→ DEALING ──→ PREFLOP_BETTING
                              │
                    ┌─────────┼──────────┐
                    ↓         ↓          ↓
              FOLD_ALL   1 PLAYER   NORMAL_END
                    │         │          │
                    ↓         ↓          ↓
                HAND_COMPLETE  ← FLOP_DEALING ──→ FLOP_BETTING
                                                │
                                                ↓
                                          TURN_DEALING ──→ TURN_BETTING
                                                                │
                                                                ↓
                                                          RIVER_DEALING ──→ RIVER_BETTING
                                                                                │
                                                                                ↓
                                                                           SHOWDOWN
                                                                                │
                                                                                ↓
                                                                           PAYOUT
                                                                                │
                                                                                ↓
                                                                           HAND_COMPLETE
```

**关键设计决策:**

| 决策 | 规则 |
|------|------|
| 最小玩家数 | 2人 |
| 最大玩家数 | 10人（满环桌9+庄观） |
| 盲注结构 | 固定盲注，可配置SB/BB/Ante |
| 底池类型 | 主池 + 无限边池 |
| All-in处理 | cap effect（短筹码全押不触发后续加注） |
| 超时处理 | 预留 timeout 钩子，未实现自动计时 |
| 牌堆 | RNG洗牌，理想应使用预洗牌机制 |

### 4.2 行动验证 (ActionValidator)

```
验证流程:
1. 基本资格检查 (玩家是否在游戏中?已行动?筹码足够?)
2. 操作类型特定验证:
   - FOLD: 总是合法（活跃状态下）
   - CHECK: 无人下注时合法
   - CALL: 需要有注可跟，金额足够
   - BET: 无人下注时合法，金额 ≥ 大盲
   - RAISE: 至少加一个大盲，考虑cap effect
   - ALL_IN: 总是合法（即使金额不足最小加注）
3. 金额修正: 若全押金额不足，修正为剩余筹码
4. 返回: ValidationResult { valid, adjusted_amount, error }
```

**Cap Effect 规则:**
```
当有玩家全押金额 < (当前下注 + 最小加注) 时:
├── 该全押玩家进入 side pot
├── 后续玩家仍然可以:
│   ├── 跟注到全押金额
│   └── 全押自己的筹码
└── 但不能在全押玩家和当前注之间加注
    (因为无法迫使已全押玩家跟更多注)
```

### 4.3 底池管理 (PotManager)

```
分层剥离算法:

玩家A: 投注100  ─┐
玩家B: 投注50   ─┤→ Layer 0: 3人×50 = 150 (所有人有资格)
玩家C: 投注100  ─┘

继续:
玩家A: 剩余50   ─┐
玩家C: 剩余50   ─┤→ Layer 1: 2人×50 = 100 (A,C有资格)
玩家B: 已用完   ─┘

最终:
┌──────────┬──────────┬───────────┐
│ 主池      │ 边池      │ 总计       │
│ $150     │ $100     │ $250     │
│ 3人资格   │ 2人资格   │          │
└──────────┴──────────┴───────────┘
```

### 4.4 摊牌结算 (ShowdownEvaluator)

```
结算流程:
1. 每个有资格玩家构建7张牌 (2张私有 + 5张公共)
2. 枚举 C(7,5)=21 种5张牌组合
3. 使用 HandRank 评估每种组合
4. 取最佳 HandRank 作为玩家最终牌力
5. 按 HandRank 排序 → 确定赢家
6. 同 rank → 平分该底池
7. 每个底池独立结算

HandRank 等级 (从高到低):
Royal Flush → Straight Flush → Four of a Kind → 
Full House → Flush → Straight → 
Three of a Kind → Two Pair → One Pair → High Card
```

### 4.5 牌桌 (Table)

```
Table = 顶层管理者
├── 管理玩家入座/离座
├── 控制游戏流程 (StartHand → ProcessAction → AdvanceStreet)
├── 处理投注逻辑 (盲注/正常下注)
├── 事件广播 (通过回调)
├── 暴露 JSON API 用于UI更新
└── 不包含AI逻辑 (AI通过外部调用 ProcessAction)
```

---

## 5. AI 分析引擎设计

### 5.1 架构概览

```
AI/Analysis Pipeline:
                  
  Input                    Analysis                    Output
  ─────                    ─────────                   ──────
  Hand History     →    Phase 1-4 Parser          →  Structured Data
  Range Spec       →    Phase 2-3 Range Builder    →  Equity Matrix
  Spot Data        →    Phase 3 Spot Solver        →  Strategy Map
  Hand History DB  →    Phase 5 Bulk Analyzer      →  Statistical Report
  Ranges           →    Phase 7-8 CFR Solver       →  GTO Strategy
  Live Data        →    Phase 9 Variance Engine    →  SWRR/CI Analysis
  Position Data    →    Phase 10 LUT Builder       →  Preflop Table
  Game State       →    Phase 11 Decision Engine   →  Action Recommendation
  Opponent History →    Phase 7 Opponent Modeler   →  Opponent Profile
```

### 5.2 求解器对比

| 求解器 | 类型 | 速度 | 精度 | 适用场景 |
|--------|------|------|------|----------|
| LUT查表 | 查表 | ⚡⚡⚡⚡⚡ | ★★☆ | 翻前推荐 |
| MC-CFR | 采样CFR | ⚡⚡⚡ | ★★★★ | 翻后spot |
| DCFR | 折扣CFR | ⚡⚡ | ★★★★★ | 长期最优 |
| ICFR | 模仿CFR | ⚡⚡⚡ | ★★★★ | 快速收敛 |
| ParallelCFR | 并行MC-CFR | ⚡⚡⚡⚡ | ★★★☆ | 实时决策 |
| PreflopGTO | 翻前GTO | ⚡ | ★★★★★ | 翻前理论 |

### 5.3 决策引擎 (DecisionEngine)

```
决策流程:
1. 接收 GameContext { hero_cards, board, pot, to_call, street, position, villain_profile }
2. 根据 DecisionLevel 选择求解器:
   - QUICK:    LUT查表 (<1s)
   - STANDARD: MC-CFR (~10s)
   - DEEP:     完整CFR (~30s)
   - RESEARCH: 大量迭代 (~60s+)
3. 求解 → 输出 DecisionResult:
   { recommended_action, confidence, expected_value, action_ev[], equity, exploitability }
4. 应用于: AI自动牌桌 / 实时HUD建议 / 分析回顾
```

---

## 6. 网络层设计

### 6.1 WebSocket 服务器

```
Server Architecture:
                    ┌──────────────┐
  Clients ────────▶│ WS Server    │
  (Web Browser)    │ (port 8080)  │
                    └──────┬───────┘
                           │ JSON Messages
                    ┌──────▼───────┐
                    │ Table Manager │───▶ Game Engines
                    │ (per table)   │     (phase12)
                    └──────┬───────┘
                           │ Events
                    ┌──────▼───────┐
                    │ Broadcast    │
                    │ (to clients) │
                    └──────────────┘

Message Flow:
Client → WS → Server → GameEngine → Server → WS → Client(s)
```

### 6.2 消息协议 (JSON)

```json
// 客户端→服务器
{ "type": "action", "player_id": 1, "action": "raise", "amount": 200 }

// 服务器→客户端
{ "type": "action_result", 
  "player_id": 1, "player_name": "Alice",
  "action": "RAISE", "amount": 200, "pot_after": 450 }
```

### 6.3 AI 自动牌桌

```
Tournament Simulator (poker_table_ai):
├── N个AI玩家 (可配置难度)
├── 自动管理盲注级别
├── 统计: 胜率/BB/100/VPIP/PFR/AF
├── 输出: 排行榜 + 每手牌日志
└── 用途:
    ├── 策略验证 (新策略 vs 旧策略)
    ├── 参数调优 (侵略性/诈唬频率)
    └── 大规模模拟 (万手牌级别)
```

---

## 7. 数据库设计

### 7.1 Entity-Relationship

```
┌──────────────┐     ┌──────────────────┐     ┌──────────────┐
│   players     │     │  player_results  │     │    hands     │
├──────────────┤     ├──────────────────┤     ├──────────────┤
│ player_id PK │◄─── │ player_id FK     │     │ hand_id PK   │
│ name         │     │ hand_id FK       │◄─── │ session_id   │
│ display_name │     │ player_name      │     │ table_name   │
│ total_buy_in │     │ hole_cards       │     │ num_players  │
│ total_net    │     │ action_summary   │     │ sb/bb/ante   │
│ created_at   │     │ amount_won       │     │ community    │
└──────────────┘     │ net_profit       │     │ duration_ms  │
                     │ won              │     └──────────────┘
                     │ best_hand        │           ▲
                     │ is_hero          │           │
                     └──────────────────┘     ┌─────┴──────┐
                                              │   actions  │
                                              ├────────────┤
                                              │ action_id  │
                                              │ hand_id FK │
                                              │ player_id  │
                                              │ street     │
                                              │ action_type│
                                              │ amount     │
                                              │ pot_after  │
                                              └────────────┘

┌──────────────────┐     ┌──────────────────┐
│   sessions       │     │  tournaments (future)
├──────────────────┤     ├──────────────────┤
│ session_id PK    │     │ tournament_id PK │
│ start_time       │     │ name             │
│ end_time         │     │ buy_in           │
│ table_name       │     │ status           │
│ num_hands        │     └──────────────────┘
└──────────────────┘
```

### 7.2 索引策略

```sql
-- 频繁查询优化
CREATE INDEX idx_hands_timestamp ON hands(timestamp);
CREATE INDEX idx_pr_hand ON player_results(hand_id);
CREATE INDEX idx_pr_player ON player_results(player_id);
CREATE INDEX idx_actions_hand ON actions(hand_id);
CREATE INDEX idx_actions_player_street ON actions(player_id, street);

-- 统计查询优化
CREATE INDEX idx_pr_won ON player_results(won);
CREATE INDEX idx_pr_net ON player_results(net_profit);
```

### 7.3 迁移管理

```
版本历史:
┌──────┬──────────────────────────────────────────────┐
│ Ver  │ 变更内容                                       │
├──────┼──────────────────────────────────────────────┤
│  1   │ 初始Schema: hands, player_results, actions     │
│      │ players表                                      │
│  2   │ 添加 position 字段到 player_results, actions   │
│  3   │ 添加 sessions表, hands.session_id              │
│  4   │ 添加 notes/tags 字段                           │
│  5   │ 添加 tournaments表 + tournament_id到hands      │
└──────┴──────────────────────────────────────────────┘
```

---

## 8. CLI 工具设计

### 8.1 工具矩阵

| 工具 | 模块 | 功能 |
|------|------|------|
| `replay_tool` | phase1 | 重放手牌历史 |
| `range_tool` | phase2 | 范围分析/构建 |
| `phase3_tool` | phase3 | 翻牌探索/Spot求解 |
| `phase4_tool` | phase4 | 手牌历史完整解析 |
| `phase5_tool` | phase5 | 批量分析/Equity矩阵/ICM |
| `phase6_tool` | phase6 | ICFR求解/范围追踪 |
| `phase7_tool` | phase7 | CFR+/锦标赛ICM/对手建模 |
| `phase8_tool` | phase8 | MC-CFR Deep/翻前GTO/仪表盘 |
| `phase9_tool` | phase9 | 方差分析/策略对比/管道 |
| `phase10_tool` | phase10 | 并行加速/LUT构建/基准测试 |
| `unified_tool` | phase11 | 统一查询/自然语义接口 |
| `poker_server` | phase13 | WebSocket游戏服务器 |
| `poker_table_ai` | phase13 | AI锦标赛模拟器 |
| `db_tool` | phase14 | 数据库管理/统计/报告 |

### 8.2 `db_tool` 使用示例

```bash
# 初始化数据库
db_tool init poker.db

# 健康检查
db_tool health

# 查看统计
db_tool stats

# 玩家排行榜
db_tool leaderboard 10

# 玩家详细报告
db_tool player 42

# 方差分析
db_tool variance 42

# 按位置拆分
db_tool positions 42

# 对手分解
db_tool opponents 42

# 最近手牌
db_tool hands 50

# 某手牌详情
db_tool hand 1234

# 自定义SQL
db_tool query "SELECT player_name, AVG(net_profit) as avg_net FROM player_results JOIN players ON player_results.player_id = players.player_id GROUP BY player_name ORDER BY avg_net DESC LIMIT 5"
```

---

## 9. Web 前端设计

### 9.1 界面布局

```
┌─────────────────────────────────────────────────────────────┐
│  🎴 Poker Engine v1.2  │  Table 1 │ Hand #42 │ Showdown   │ ← Header
├─────────┬─────────────────────────────────┬─────────────────┤
│ 👥      │                                 │ ⚡ Action       │ │
│ Player1 │      🟢  Table View             │   [Fold] [Chk] │ │
│ $1,200  │     ┌─────────────┐             │   [Call] [$30] │ │
│ 👤 AI1  │     │  Ah  Kh  7d  │             │   [Bet] [$50]  │ │
│ $950    │     │  Jd  3c     │             │   [Raise][$100]│ │
│ 👤 AI2  │     │  2h         │             │   [All-In]     │ │
│ $2,100  │     └─────────────┘             │                 │ │
│ D       │     Pot: $85                    │ 📜 Log          │ │
│ BB      │                                │ Alice: raises   │ │
│ $1,500  │                                │ Bob: calls      │ │
│         │                                │ ...             │ │
├─────────┴─────────────────────────────────┴─────────────────┤
│ 状态栏: 连接中... 延迟: 42ms                              │
└─────────────────────────────────────────────────────────────┘
```

### 9.2 技术实现

```
前端: 纯 HTML/CSS/JS (无框架依赖)
渲染: CSS Grid + Flexbox
通信: Native WebSocket API
特效: CSS animations for card dealing
依赖: 无 (单个HTML文件可直接打开)
```

---

## 10. 通信协议

### 10.1 WebSocket 消息类型

```
C2S (Client → Server):
├── join         { type: "join", name, buy_in, table? }
├── sit          { type: "sit", seat }
├── stand        { type: "stand" }
├── leave        { type: "leave" }
├── action       { type: "action", action: "fold|check|call|bet|raise|allin", amount? }
├── start_hand   { type: "start_hand" }
├── chat         { type: "chat", message }
├── get_state    { type: "get_state" }
├── list_tables  { type: "list_tables" }
└── ai_suggest   { type: "ai_suggest" }

S2C (Server → Client):
├── welcome         { type: "welcome", player_id, message }
├── player_joined   { type: "player_joined", name, player_id }
├── player_left     { type: "player_left", player_id }
├── hand_start      { type: "hand_start", hand_id }
├── cards_dealt     { type: "cards_dealt", player_id, cards }
├── post_blinds     { type: "post_blinds", sb, bb }
├── turn_action     { type: "turn_action", player_id, to_call, pot }
├── action          { type: "action", player_name, action, amount, pot_after }
├── community_cards { type: "community_cards", cards, round }
├── showdown        { type: "showdown", player_name, cards, hand_name }
├── payout          { type: "payout", player_name, amount, hand_name }
├── hand_end        { type: "hand_end" }
├── full_state      { type: "full_state", data: JSON }
├── chat            { type: "chat", player_name, message }
├── error           { type: "error", message }
└── state_update    { type: "state_update", data }
```

### 10.2 二进制协议 (未来扩展)

```
如需更高性能，可替换JSON为二进制协议:
┌──────┬──────┬──────────┬───────────┐
│ Magic│ Type │ Length   │ Payload   │
│0x504B│ 1B   │ 2B       │ N bytes   │
└──────┴──────┴──────────┴───────────┘
适用于: 高频交易类实时游戏
```

---

## 11. 构建与部署

### 11.1 构建

```bash
# 克隆/进入项目
cd ~/my-project/projects/poker-engine

# 清理构建
rm -rf build && mkdir build && cd build

# 配置 (依赖已 vendored，无需网络)
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      .. 2>&1

# 编译
cmake --build . -j$(nproc)

# 运行测试
ctest --output-on-failure
# 或直接运行测试二进制:
./tests/poker_tests --gtest_filter="EvaluatorTest.*:CardTest.*"
```

### 11.2 依赖

> 所有 C++ 依赖已 vendored 到 `third_party/`，构建无需网络访问。

| 依赖 | 必需 | 用途 | 来源 |
|------|------|------|------|
| CMake ≥ 3.16 | ✅ | 构建系统 | 系统 |
| C++20 编译器 | ✅ | 语言标准 (GCC 12+ / Clang 15+) | 系统 |
| Threads | ✅ | 多线程 | 系统 |
| SQLite3 | ✅ | 数据持久化 | 系统 |
| PokerHandEvaluator | ✅ | 手牌评估 | vendored |
| nlohmann/json | ✅ | JSON 序列化 | vendored |
| spdlog | ✅ | 日志 | vendored (header-only) |
| fmt | ✅ | 格式化 | vendored (header-only) |
| GoogleTest | ✅ | 单元测试 | vendored (预编译) |
| httplib | ✅ | HTTP 服务器 | vendored (单头文件) |
| OpenSSL | 可选 | WebSocket 握手 (SHA-1, Base64) | 系统 |
| OpenMP | 可选 | 并行化加速 | 系统 |
| Eigen | 可选 | 矩阵运算 (phase18) | 系统 |
| FlatBuffers | 可选 | 二进制序列化 (phase20) | 系统 |
| pybind11 | 可选 | Python 绑定 | 系统 |
| Boost | 可选 | 未来网络层替代 | 系统 |

### 11.3 安装后使用

```bash
# 1. 启动游戏服务器
./cli/poker_server 8080

# 2. 浏览器打开 web/client/index.html
#    输入名字 → 加入牌桌

# 3. 或启动AI锦标赛
./cli/poker_table_ai -p 6 -h 500 -s 10000

# 4. 数据库管理
./cli/db_tool init poker.db
./cli/db_tool stats
./cli/db_tool leaderboard
```

### 11.4 Docker 部署 (推荐)

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    build-essential cmake libsqlite3-dev libssl-dev
COPY . /app
RUN cd /app/build && cmake -DCMAKE_BUILD_TYPE=Release .. \
    && cmake --build . -j$(nproc)
EXPOSE 8080
CMD ["./cli/poker_server", "8080"]
```

---

## 12. 开发指南

### 12.1 代码规范

```
命名约定:
├── 类型名: PascalCase (GameState, HandRepository)
├── 成员变量: m_ 前缀 (m_db_, m_players_)
├── 局部变量: camelCase (playerCount, handId)
├── 常量: k 前缀 (kMaxPlayers = 10)
├── 宏: 全大写 (MAX_PLAYERS)
├── 文件名: snake_case (game_state.h, game_state.cpp)
└── 测试文件: <module>_test.cpp

代码风格:
├── 命名空间: 嵌套使用 poker_engine::module
├── RAII: 所有资源 (DB连接/事务/锁) 必须用RAII包装
├── 错误处理: 返回 Result/optional, 避免裸指针
├── 线程安全: 共享状态必须加锁 (std::mutex)
└── 注释: 接口注释 (Doxygen风格), 复杂算法注释原理
```

### 12.2 添加新模块

```
步骤:
1. 创建目录 phaseN/{include,src}
2. 实现头文件和源文件
3. 创建 phaseN/CMakeLists.txt (add_library)
4. 在根 CMakeLists.txt 中 add_subdirectory(phaseN)
5. 在依赖方 target_link_libraries(... phaseN)
6. 创建 tests/phaseN_test.cpp
7. 更新 DESIGN.md 和 README.md
```

### 12.3 测试约定

```
├── 每个模块必须有对应的测试文件
├── 测试命名: TEST(ModuleTest, TestName)
├── 使用 SCOPED_TRACE 提供上下文
├── 边界测试: 空输入、极大值、零值
├── 并发测试: 对共享状态进行多线程访问
├── 性能基准: 使用 phase10 的 BenchmarkRunner
└── 测试数据: 使用确定性种子 (srand(42))
```

---

## 13. API 参考

### 13.1 GameState API

```cpp
class GameState {
public:
    // 生命周期
    void StartHand();                     // 开始一手牌
    void ProcessAction(int32_t pid, const GameAction&);  // 处理行动
    
    // 查询
    GamePhase GetPhase() const;           // 当前阶段
    bool IsHandInProgress() const;        // 牌局是否进行中
    bool CanAct(int32_t pid) const;       // 能否行动
    double GetCurrentBet() const;         // 当前下注额
    double GetPot() const;                // 底池总额
    const CommunityCards& GetCommunity() const; // 公共牌
    int ActivePlayerCount() const;        // 活跃玩家数
};
```

### 13.2 ActionValidator API

```cpp
class ActionValidator {
public:
    static ValidationResult Validate(
        const GameAction& action,
        const PlayerState& player,
        const std::vector<PlayerState*>& active,
        double current_bet, double pot,
        double big_blind, double ante,
        int num_active, int num_allin, int street);
    
    static MinRaiseInfo CalculateMinRaise(
        const PlayerState& p, double current_bet,
        double pot, double big_blind,
        const std::vector<PlayerState*>& all, int street);
};
```

### 13.3 Database API

```cpp
class Database {
public:
    bool Open(const std::string& path);
    void Close();
    bool Execute(const std::string& sql);
    ResultSet Query(const std::string& sql);
    void BeginTransaction();
    void Commit();
    void Rollback();
};

class HandRepository {
public:
    int64_t SaveHand(const HandRecord& hand,
                     const std::vector<PlayerHandResult>& results,
                     const std::vector<ActionRecord>& actions);
    std::pair<HandRecord, std::vector<PlayerHandResult>> GetHand(int64_t hand_id);
    std::vector<ActionRecord> GetHandActions(int64_t hand_id);
    std::vector<HandRecord> ListHands(int limit, int offset);
    std::vector<PlayerHandResult> GetPlayerHands(int32_t pid, int limit);
};

class PlayerRepository {
public:
    int32_t CreatePlayer(const std::string& name);
    PlayerInfo GetPlayer(int32_t id);
    std::vector<PlayerInfo> FindPlayers(const std::string& pattern);
    void UpdatePlayerStats(int32_t id, double buy_in, double cash_out);
    std::vector<std::pair<PlayerInfo, PlayerStats>> GetLeaderboard(int limit);
};

class StatRepository {
public:
    OverviewStats GetOverviewStats();
    VarianceStats GetVarianceStats(int32_t pid);
    std::vector<PositionalStats> GetPositionalStats(int32_t pid);
    std::vector<OpponentStat> GetOpponentStats(int32_t pid);
    std::vector<TimeRangeStats> GetDailyStats(int32_t pid);
};
```

---

## 14. 演进路线

### Phase 1: 已完成 (v1.0)
- ✅ 核心类型系统 (Card, Deck, Evaluator)
- ✅ 手牌历史解析 (Phase 1-4)

### Phase 2: 已完成 (v1.1)
- ✅ 范围构建 + CFRA
- ✅ CLI 工具 (replay, range, phase3-4)

### Phase 3: 已完成 (v1.1)
- ✅ 翻牌探索 + Spot Solver
- ✅ 批量解析 + 回放

### Phase 4: 已完成 (v1.1)
- ✅ 完整手牌历史解析
- ✅ 多街CFRA + 会话分析

### Phase 5: 已完成 (v1.1)
- ✅ 批量引擎 + Equity Matrix
- ✅ ICM + 回归分析 + 手牌生成

### Phase 6: 已完成 (v1.1)
- ✅ ICFR 求解器
- ✅ SQLite 数据库 + REST API

### Phase 7: 已完成 (v1.1)
- ✅ CFR+ (3种变体) + 锦标赛ICM
- ✅ 对手建模 + Python绑定 + MTT模拟

### Phase 8: 已完成 (v1.1)
- ✅ MC-CFR Deep + 翻前GTO求解器
- ✅ 剥削度引擎 + 仪表盘 + 多智能体模拟

### Phase 9: 已完成 (v1.1)
- ✅ 方差引擎 + 策略差异分析
- ✅ 极化范围 + 全链路管道

### Phase 10: 已完成 (v1.1)
- ✅ OpenMP/线程池并行化
- ✅ 预翻LUT (169×169)
- ✅ 并行批量解析

### Phase 11: 已完成 (v1.1)
- ✅ 统一Solver Manager
- ✅ LUT加速决策引擎
- ✅ 引擎注册中心

### Phase 12: 已完成 (v1.2)
- ✅ 完整游戏状态机 (22阶段)
- ✅ 底池/边池管理 (含全押)
- ✅ 行动验证 (含cap effect)
- ✅ 摊牌结算 (含平分)
- ✅ 发牌引擎

### Phase 13: 已完成 (v1.3)
- ✅ WebSocket 服务器 (含握手/编解码)
- ✅ AI 自动决策 (4级难度)
- ✅ AI 锦标赛模拟器
- ✅ Web 前端 (3D牌桌)

### Phase 14: 已完成 (v1.4)
- ✅ SQLite3 持久化 (WAL模式)
- ✅ 5级自动Schema迁移
- ✅ 手牌/玩家/统计 仓库
- ✅ 链式 SQL 查询构建器
- ✅ 数据库 Facade 管理器

### Phase 15-19: 已完成 (v1.5)
- ✅ CFR 引擎 + 抽象 + 锦标赛管理 (phase15)
- ✅ 回放引擎 + 反作弊 + 管理接口 (phase16)
- ✅ 观战管理 + ML 引擎 + 直播 (phase17)
- ✅ 超参调优 + 贝叶斯优化 + 负载均衡 (phase18)
- ✅ 独立手牌评估器 + 审计日志 + 限速器 (phase19)

### Phase 20-21: 已完成 (v1.6)
- ✅ FlatBuffer 序列化 + CFR 节点 + 策略网络 (phase20, FlatBuffers vendored)
- ✅ 加密工具 + GDPR + IP 信誉 + mTLS (phase21, 全部编译)

### 未来计划 (v1.6+)

| 优先级 | 功能 | 预估 |
|--------|------|------|
| P0 | ~~手牌重放器 (Web端)~~ ✅ 已完成 | — |
| P1 | 锦标赛管理 (多桌+淘汰) | 1周 |
| P1 | ~~实时HUD (覆盖层)~~ ✅ 已完成 | — |
| P2 | 多人锦标赛 (局域网) | 2周 |
| P2 | 持久化玩家账户系统 | 3天 |
| P3 | Omaha 游戏支持 | 2周 |
| P3 | 观战模式 + 直播 (phase17 接入前端) | 1周 |
| P1 | 排行榜页面（REST API 已就绪） | 3天 |
| P4 | 比赛匹配系统 | 2周 |
```

---

## 附录: 项目统计

```
📊 截至 v1.5
├── 模块:         22 个 (core + equity + phase1-21)
├── 头文件:       ~130 个
├── 源文件:       ~230 个
├── 总LOC:        ~62,000+
├── 测试:         350 (快速) + 170+ (慢速)
├── CLI工具:      14 个
├── 数据库表:     8 张 (+2张future)
├── Schema版本:   v5
└── 覆盖率:       ~85% (核心模块), clang-format 全量通过

🎯 核心能力:
✅ No-Limit Texas Hold'em
✅ Cash Game + 锦标赛模拟
✅ AI决策 (7种求解器, 4级难度)
✅ 多人游戏 (WebSocket + 本地)
✅ 数据持久化 + 统计分析
✅ 数据库自动迁移
✅ 链式查询构建
✅ 完整的游戏规则实现 (底池/边池/全押/cap effect/平分/盲注)
✅ 安全层 (反作弊/GDPR/mTLS/IP信誉)
✅ CFR 扩展 (抽象/训练/FlatBuffer序列化)
✅ 依赖 vendored (离线构建)
```

---

> 这个文档应该随代码一起提交到仓库，作为 `docs/DESIGN.md`。每次大版本更新时更新此文档。
