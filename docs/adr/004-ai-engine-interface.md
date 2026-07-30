# ADR-004 AI 决策引擎接口

| 字段         | 值                                          |
|-------------|---------------------------------------------|
| 编号        | 004                                         |
| 标题        | AI Engine 接口契约与决策协议                  |
| 状态        | 📝 Draft                                     |
| 日期        | 2026-06-15                                   |
| 决策者      | James                                       |

---

## 1. 上下文

phase13 的 `ai_engine.cpp` 目前是 stub，需要定义明确的输入/输出/延迟约束。AI 引擎需要同时支持：
- **Rule-based**（上线时的最小可用方案）
- **CFR-based**（phase7-9 训练的策略模型，上线后逐步替换）

两者通过统一接口对外暴露，游戏引擎不感知内部实现。

## 2. 接口契约

### 2.1 C++ 抽象接口

```cpp
// phase13/include/poker_engine/network/ai_engine.h

#include <poker_engine/game/action.h>
#include <poker_engine/game/game_state.h>

namespace poker_engine {
namespace ai {

struct AIConfig {
    std::string strategy_type;   // "rule_based" | "cfr_model" | "random"
    int64_t time_limit_ms;       // 最大决策时间（毫秒）
    std::string model_path;      // CFR 模型路径（仅 cfr 类型）
    float aggression;            // 激进系数 0.0~2.0（rule_based 使用）
    float tightness;             // 松紧度 0.0~2.0（rule_based 使用）
};

struct DecisionRequest {
    Observation observation;     // 脱敏的每玩家观测（仅含自己底牌）
    PlayerId player_id;          // 需要决策的玩家
    std::vector<Action> legal_actions;  // 当前合法行动列表
};
// 注：observation 由 GameState::ObserveFor(viewer_id) 构造，只含公共状态 +
// 观察者自己的底牌；对手以 PlayerView 出现（无 hole_cards 字段），Agent 在类型
// 上无法窥探隐藏信息。OnHandComplete 仍收到完整 GameState（手末摊牌信息，公开）。

struct DecisionResponse {
    ActionType action;           // fold / check / call / bet / raise / all_in
    int64_t amount;              // 下注金额（仅 bet/raise/all_in 有效）
    int64_t decision_time_ms;    // 实际决策耗时
    float confidence;            // 置信度 0.0~1.0
    std::string reason;          // 调试信息（log用）
};

class IAIEngine {
public:
    virtual ~IAIEngine() = default;

    // 初始化（加载模型等）
    virtual void Initialize(const AIConfig& config) = 0;

    // 请求决策（同步或异步）
    virtual DecisionResponse Decide(const DecisionRequest& request) = 0;

    // 通知一手牌结束（用于学习/统计）
    virtual void OnHandComplete(const GameState& final_state) = 0;

    // 模型热更新
    virtual bool ReloadModel(const std::string& model_path) = 0;
};

} // namespace ai
} // namespace poker_engine
```

### 2.2 关键设计约束

| 约束 | 值 | 说明 |
|------|----|------|
| **最大决策延迟** | 5000ms | 超时则自动 fold（保护玩家体验） |
| **推荐延迟** | 1000~3000ms | 模拟人类思考时间，避免 bot 检测 |
| **决策超时行为** | fold + log warning | 不阻塞其他玩家 |
| **并发场景** | 单桌串行 | Table 会等待 AI 决策完成再推进 |

## 3. Rule-Based AI 策略（Phase 1 实现）

### 3.1 翻前（Preflop）策略

```
输入: 手牌强度（Hilo 等级）、位置、当前行动

决策逻辑:
1. 如果是大盲注且无人加注 → check（防守盲注）
2. 手牌等级 ≥ 阈值[位置] → raise（加注额 = 3BB~5BB）
3. 手牌等级在跟注范围 → call
4. 否则 → fold
```

**位置 vs 手牌阈值（简化版）**:

| 位置 | 加注最低等级 | 跟注最低等级 |
|------|------------|------------|
| UTG | 8（前8%） | 15（前15%） |
| MP | 10 | 20 |
| CO | 12 | 25 |
| BTN | 14 | 35 |
| SB | 16 | 40 |
| BB（防守）| - | 30 |

### 3.2 翻后（Postflop）策略

```
输入: 手牌胜率（Hilo equity%）、当前底池、下注历史

决策逻辑:
1. 计算底池赔率(pot_odds) = 需要跟注 / (底池 + 需要跟注)
2. equity > 50% 且无大额下注 → bet/raise（价值下注）
3. equity > pot_odds → call
4. equity < pot_odds 且 equity < 20% → fold
5. check（无人下注且 equity 20~50%）
```

### 3.3 随机数种子

```cpp
// Rule-based AI 加入 10% 的随机扰动，避免完全可预测
// 加注/跟注时 ±1BB 的随机浮动
std::mt19937 rng(seed_);
```

## 4. CFR-Based AI（Phase 2+ 演进）

### 4.1 数据接口

```
CFR 模型输入:
  - 抽象化游戏状态（信息集 ID）
  - 来自 phase8-9 的预计算策略网络

CFR 模型输出:
  - 动作概率分布 {fold: 0.1, call: 0.6, raise: 0.3}
  - 策略网络版本号 + 时间戳
```

### 4.2 部署方式

- **本地加载**：模型文件 (.onnx 或 .pt) 从磁盘加载
- **远程推理**（远期）：通过 gRPC 调用独立的 AI 推理服务
- **回退机制**：远程调用超时 → 降级为 rule-based

## 5. WebSocket 消息格式

当 AI 决策完成后，通过 WS 发送 action 消息：

```json
// AI → Server (内部)
{
  "type": "player_action",
  "payload": {
    "table_id": "t1",
    "player_id": "ai_001",
    "action": "raise",
    "amount": 25
  }
}
```

## 6. 性能指标

| 指标 | rule_based 目标 | cfr 目标 |
|------|----------------|----------|
| 决策延迟中位数 | < 100ms | < 500ms |
| 决策延迟 P99 | < 1000ms | < 3000ms |
| 内存占用 | < 10MB | < 500MB |
| 决策质量（Hilo profit/100hand）| 负（被剥削）| 正（长期盈利）|

## 7. 演进路径

```
Phase 1 (当期): Rule-based AI → 支持自动跑桌
Phase 2: CFR 预训练模型加载 → 策略质量提升
Phase 3: 在线学习 + 动态调整 → 适应对手风格
Phase 4: 多 Agent 自对弈 → 持续进化
```

## 8. 关联 ADR

- ADR-001: AI Engine Component 部分
- ADR-002: AI 决策通过内部 WS 消息格式传递
- ADR-003: AI 决策响应 player_action 触发状态迁移
