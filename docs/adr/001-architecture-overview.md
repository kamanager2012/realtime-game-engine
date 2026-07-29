# ADR-001 架构总览

| 字段         | 值                                      |
|-------------|----------------------------------------|
| 编号        | 001                                     |
| 标题        | Poker Engine 整体架构                   |
| 状态        | ✅ Accepted                             |
| 日期        | 2026-06-15                              |
| 决策者      | James                                  |

---

## 1. 上下文

我们需要一个**可演进**的德州扑克引擎，既能支撑 CLI 交互式调试，又能最终演化为支持 WebSocket 实时对战、AI 决策、持久化存储的在线扑克平台。

项目采用**演进式架构**（Evolutionary Architecture），按 phase 编号迭代，每 phase 解决一组内聚问题，不破坏之前的可运行代码。

## 2. 架构层级（C4 Context → Container → Component）

### 2.1 Context 级别

```
┌──────────────────────────────────────────────────────────┐
│                      外部角色                               │
│   Human Player    AI Engine    Browser Client   Database  │
└──────────┬───────────────────┬──────────────────┬─────────┘
           │                   │                  │
           ▼                   ▼                  ▼
┌──────────────────────────────────────────────────────────┐
│                    Poker Server (Container)               │
│  ┌────────────┐  ┌────────────┐  ┌────────────────────┐  │
│  │ Game Engine│  │ Network    │  │ Persistence Layer  │  │
│  │ (phase12)  │  │ (phase13)  │  │ (phase14)          │  │
│  └─────┬──────┘  └──────┬─────┘  └─────────┬──────────┘  │
│        │                │                   │             │
│  ┌─────▼────────────────▼───────────────────▼──────────┐  │
│  │              Core Library (shared)                    │  │
│  │  Evaluator │ Range │ Equity │ Serialization │ Base   │  │
│  └──────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### 2.2 Container 级别

| Container        | 技术方案                    | 职责                             |
|------------------|----------------------------|----------------------------------|
| **Game Engine**  | C++20, 头文件-only核心库    | 牌局状态机、手牌评估、底池管理、行动验证 |
| **Network Layer**| uWebSockets (待接入)        | WS 连接管理、消息路由、多桌并发      |
| **AI Engine**    | Rule-based → CFR (演进)    | 决策输出、策略查询、模型热更新        |
| **Persistence**  | SQLite → PostgreSQL (演进) | 手牌历史、玩家数据、统计指标          |
| **CLI Server**   | 交互式 stdin/stdout        | 开发调试、人工对战演示               |

### 2.3 Component 级别（Game Engine 内部）

```
Game Engine (phase12)
├── ActionValidator    — 行动合法性检查（下注额、行动顺序、全押规则）
├── Dealer             — 发牌逻辑（洗牌、发公共牌、烧牌）
├── GameState          — 桌局状态快照（阶段、底池、玩家、牌面）
├── PlayerState        — 玩家状态（筹码、手牌、行动历史、是否全押/弃牌/坐下）
├── PotManager         — 底池+边池计算（全押时分割底池）
├── ShowdownEvaluator  — 摊牌：比较各玩家手牌强度，确定赢家及分额
└── Table              — 编排者（Orchestrator），驱动一局完整流程
```

## 3. Phase 演进路线

| Phase | 模块                    | 状态       | 实现度 | 职责                               |
|-------|------------------------|-----------|--------|------------------------------------|
| 0     | Core Evaluator+Range   | ✅ Done    | 85%    | 评估器+范围引擎                     |
| 1     | Hand History / EV Replay | ✅ Done  | 80%    | 手牌历史解析与期望值回放               |
| 2     | Range Builder          | ✅ Done   | 75%    | 范围构建与花色同构                    |
| 3     | Flop Explorer          | ✅ Done   | 80%    | 翻牌圈批量模拟                        |
| 4     | HH Parser / Multi-Street Solver | ✅ Done | 85% | 手牌解析与多街求解器            |
| 5     | Equity Matrix / ICM    | ✅ Done   | 80%    | 权益矩阵与 ICM 计算                  |
| 6     | API Server / ICFR      | ✅ Done   | 75%    | API 服务 + ICFR 求解器               |
| 7     | CFR+ / MTT / Opponent Model | ✅ Done | 80% | CFR+求解/MTT模拟/对手建模     |
| 8     | MC CFR Deep / Preflop Solver | ✅ Done | 75% | 深度蒙特卡洛CFR/翻前求解器    |
| 9     | Batch Analyzer / Variance Engine | ✅ Done | 70% | 批量分析/方差引擎            |
| 10    | Parallel CFR / Preflop LUT | ✅ Done  | 75%    | 并行翻前LUT生成                      |
| 11    | Decision Engine / Fast Preflop Solver | ✅ Done | 80% | 决策引擎/快速翻前求解      |
| 12    | **Game Engine (Core)** | ✅ Done   | 90%    | **状态机、底池、边池、全押、摊牌**     |
| 13    | Network / WS / AI Stub | 🔴 In Progress | 55% | WebSocket + AI 引擎桩           |
| 14    | Database / Repository  | ✅ Done   | 90%    | 数据持久化层                          |

## 4. 核心设计原则

1. **演进式而非大爆炸**：每 phase 可独立编译、测试、回滚。
2. **接口隔离**：phase 之间通过头文件接口通信，不共享内部实现。
3. **确定性优先**：游戏引擎纯逻辑无随机（除洗牌），便于测试复现。
4. **零外部依赖（Core）**：`core/` 目录下无第三方依赖，可嵌入任意宿主。
5. **测试驱动**：每个 phase 配套测试，CI 全部绿灯才合并。

## 5. 关键抽象

```
Card       → 花色(0-3) + 点数(0-12) → bitmask 编码（紧凑、高效）
Hand       → 5~7 张 Card → 评估为 HandRank（9级，0最强）
Action     → {玩家, 类型, 金额} → 不可变值对象
GameState  → 快照式（不可变），每次行动生成新状态
Table      → 有状态编排者，驱动 GameState 迁移
```

## 6. 架构决策记录索引

| 编号 | 标题 | 状态 |
|------|------|------|
| ADR-001 | 架构总览 | ✅ Accepted |
| ADR-002 | 网络通信协议 | 📝 Draft |
| ADR-003 | 游戏状态机设计 | 📝 Draft |
| ADR-004 | AI 决策引擎接口 | 📝 Draft |
| ADR-005 | 数据库持久化方案 | 📝 Draft |
