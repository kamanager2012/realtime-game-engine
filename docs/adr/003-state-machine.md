# ADR-003 游戏状态机设计

| 字段         | 值                                        |
|-------------|-------------------------------------------|
| 编号        | 003                                       |
| 标题        | Texas Hold'em 游戏状态机完整设计            |
| 状态        | 📝 Draft                                   |
| 日期        | 2026-06-15                                 |
| 决策者      | James                                     |

---

## 1. 上下文

phase12 的游戏引擎已实现基本状态机，但在 **3人桌 Preflop 第一手牌 Call 被拒** 的 edge case 暴露了行动顺序和状态迁移逻辑的不完善。本文档精确定义所有状态、迁移条件、行动合法性规则。

## 2. 状态定义

```
enum class GamePhase {
    Preflop,    // 翻前（发给每个玩家2张手牌后）
    Flop,       // 翻牌圈（发3张公共牌）
    Turn,       // 转牌圈（发第4张公共牌）
    River,      // 河牌圈（发第5张公共牌）
    Showdown,   // 摊牌
    HandOver    // 本手牌结束，准备下一手
};

enum class BettingRound {
    Preflop,
    Flop,
    Turn,
    River,
    Complete    // 本轮下注结束（所有人都跟注/过牌，或只剩一人）
};
```

## 3. 状态迁移图

```
    ┌─────────┐   所有坐下    ┌──────────┐   发底牌   ┌───────────┐
    │ HAND    │ ──────────▶  │ PREFLOP  │ ────────▶ │ POST_SB   │
    │ OVER    │              │ (wait)   │           │ (actions) │
    └────▲────┘              └────┬─────┘           └─────┬─────┘
         │                       │                       │
         │  手牌结束              │  3人桌SB+BB都all-in?   │ 全部跟注/过牌
         │                       │  → 直接 Showdown       │ → 迁移
         │                       ▼                       ▼
         │                ┌──────────┐           ┌───────────┐
         │                │POST_BB   │           │  FLOP     │
         │                │(actions) │           │(发牌+actions)│
         │                └────┬─────┘           └─────┬─────┘
         │                     │                       │
         │                     │  全部完成              │  全部完成
         │                     ▼                       ▼
         │                ┌──────────┐           ┌───────────┐
         │                │  FLOP   │ ─────────▶ │  TURN     │
         │                │(发3张)   │           │(发第4张)   │
         │                └──────────┘           └─────┬─────┘
         │                                              │  全部完成
         │                                              ▼
         │                ┌──────────┐           ┌───────────┐
         │                │ SHOWDOWN │ ◀──────── │  RIVER    │
         │                └────┬─────┘           │(发第5张)   │
         │                     │                  └─────┬─────┘
         │  结算完成            │  全部完成                 │
         ▼                     ▼                          ▼
    ┌─────────┐        ┌──────────┐                ┌───────────┐
    │ HAND    │        │SHOWDOWN  │ ─────────────▶ │ HAND_OVER │
    │ OVER    │        └──────────┘   确定赢家     │(结算+准备) │
    └─────────┘                                   └───────────┘
```

## 4. 行动顺序（Action Order）

### 4.1 基础规则

**标准行动顺序**（顺时针，从 Button 左侧第一个未弃牌玩家开始）：

```
Button (庄家)位置 = 每手牌顺时针移动一位

Preflop 行动起点:
  - 2人桌: SB(先行动) → BB → (如果没人加注则BB结束)
  - 3+人桌: UTG(最先行动) → ... → SB → BB → (如果有人加注则继续)

Post-flop 行动起点:
  - 小盲位左侧第一个未弃牌玩家（若只剩1人则直接发牌跳过）
  - 如果所有人在flop前都已all-in，则跳过flop行动直接翻牌
```

### 4.2 行动类型

| 行动 | 条件 | 效果 |
|------|------|------|
| **Fold** | 任何轮 | 弃牌，放弃本手牌权益 |
| **Check** | 当前已跟注 or bet为0 | 过牌，不投入筹码，行动权传递 |
| **Call** | 前方有bet/raise | 跟注至当前最高额 |
| **Bet** | 当前bet为0（没人下注） | 发起下注 |
| **Raise** | 前方有bet/raise | 加注（上家金额的 min raise 倍数） |
| **All-in** | 筹码不足 call 全额时 | 全押（不完整加注，可能 reopen 投注轮） |

### 4.3 下注轮结束条件

一轮下注结束 = **所有未弃牌、未全押玩家对本轮已投入相同筹码**，且**都至少行动过一次**。

```
<下注轮结束> 当且仅当:
  ∀p ∈ 未弃牌玩家:
    p.last_action_round = current_round AND
    (p.is_all_in OR p.current_bet = max_current_bet)
```

### 4.4 All-in 特殊规则

**完整加注（Complete Raise）** vs **不完整加注（Incomplete Raise）**:

```
if (all_in_amount < min_raise) {
    // 不完整加注 → 不 reopen 投注轮
    // 已行动玩家不能再次行动
} else {
    // 完整加注 → reopen 投注轮
    // 其他未弃牌未全押玩家可再次行动
}

min_raise = 上一次有效加注额 + (上一次有效加注额 - 上上一次)
           即: 至少是前一次 raise size
```

**边池（Side Pot）规则**:

```
当玩家 A 全押（投入 X）：
  - 底池：创建主池，上限为所有跟注者中最低总额
  - 边池：如果有人投入 > X，多余部分形成边池
  - A 只能赢得主池（≤ X * 未弃牌人数）
  - 边池由投入更多的玩家之间竞争
```

## 5. Edge Case 集合

### 5.1 3人桌 Preflop 第一手牌

**Bug描述**：UTG 玩家 Call SB 的 raise 被拒。

**根因**：GameState 初始化时，`players_in_action` 未正确排除只在大盲注位置的玩家，导致行动轮次计算跳过了 BB。

**修复逻辑**：
```
preflop 第一轮行动:
  起点 = UTG (Button 左侧第一个仍有筹码且未弃牌的玩家)
  终点 = BB (大盲注玩家)
  BB 如果只下盲注未行动 → 仍可行动（call/raise/fold）
  BB 如果已经过牌（check）→ 轮次结束
```

### 5.2 全押后重新开始投注轮

当一名玩家在翻牌圈全押（不完整加注），已跟注的玩家**不能**再次行动，除非有后续玩家再加注。

```
示例: 翻牌圈 A bet 10, B call 10, C all-in 15(不足min_raise)
  → C 不 reopen
  → A 和 B 不能再次行动
  → 投注轮继续给 C 之后的玩家（如果还有）
```

### 5.3 所有玩家全押

如果所有玩家都全押（无论是否在同一轮），**立即进入摊牌**，不再发后续公共牌（除非已有足够公共牌）。

**特殊情况**：Preflop 全押 → 直接进入摊牌（0张公共牌），需要发3张 → 总共发5张。

### 5.4 单人游戏（仅剩一人未弃牌）

如果只剩一个未弃牌玩家：
```
1. 不进行摊牌比较
2. 该玩家赢得底池中所有筹码
3. 跳过发后续公共牌
4. hand_event → 标记为 "uncontested pot"
```

## 6. GameState 不可变快照

每次行动后生成新的 `GameState` 实例（不可变），包含完整历史：

```
GameState {
    phase:          GamePhase,
    dealer_pos:     u8,
    community_cards: Vec<Card>,
    pot:            PotManager,       // 底池+边池
    players:        Vec<PlayerState>, // 所有玩家状态
    current_player: usize,            // 当前行动者索引
    action_history: Vec<ActionRecord>,
    bets_this_round: Vec<u32>,         // 本轮各玩家已投注额
    hand_number: u32,
}
```

## 7. 关联 ADR

- ADR-001: Game Engine Component 部分
- ADR-002: player_action 消息触发状态迁移
