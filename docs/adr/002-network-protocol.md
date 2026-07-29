# ADR-002 网络通信协议

| 字段         | 值                                          |
|-------------|---------------------------------------------|
| 编号        | 002                                         |
| 标题        | WebSocket 通信协议设计与消息格式              |
| 状态        | 📝 Draft                                     |
| 日期        | 2026-06-15                                   |
| 决策者      | James                                       |

---

## 1. 上下文

phase13 的 `websocket_server.cpp` 目前是 stub，不支持真实连接。需要定义一套客户端↔服务端的消息协议，作为后续实现 WS 通信、蓝绿部署、多桌路由的基础。

## 2. 传输层选型

| 选项 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| **uWebSockets** | 超高性能、低内存、原生 C++ | API 较新，学习曲线陡 | ⭐⭐⭐⭐⭐ |
| Boost.Beast | 成熟、稳定、生态好 | 较重，性能略低 | ⭐⭐⭐⭐ |
| cpp-httplib | 已集成、简单 | WS 功能有限 | ⭐⭐⭐ |
| raw WebSocket | 完全控制 | 重复造轮子 | ⭐⭐ |

**决策**：首选 **uWebSockets**，当前用 **cpp-httplib** 过渡。

## 3. 消息格式（JSON over WebSocket）

### 3.1 通用信封

```json
{
  "type": "<message_type>",
  "seq": 123,
  "timestamp": "2026-06-15T10:30:00.000Z",
  "payload": { ... }
}
```

- `type`：消息类型枚举（字符串，见下文）
- `seq`：客户端序列号，用于请求-响应匹配
- `timestamp`：ISO 8601，服务端收到后重写
- `payload`：类型特定数据

### 3.2 客户端 → 服务端 消息类型

| type | 描述 | payload 字段 |
|------|------|-------------|
| `join_table` | 加入牌桌 | `{ table_id, player_name, seat_index?, buy_in? }` |
| `leave_table` | 离开牌桌 | `{ table_id }` |
| `player_action` | 玩家行动 | `{ table_id, action: "fold\|call\|raise\|check\|all_in", amount? }` |
| `chat_message` | 聊天 | `{ table_id, message }` |
| `subscribe` | 订阅牌桌更新 | `{ table_id }` |
| `unsubscribe` | 取消订阅 | `{ table_id }` |
| `heartbeat` | 心跳 | `{}` |

### 3.3 服务端 → 客户端 消息类型

| type | 描述 | payload 字段 |
|------|------|-------------|
| `table_state` | 完整桌局状态推送 | `{ table_id, game_state, players[], pot, community_cards, current_player, phase }` |
| `player_joined` | 有玩家加入 | `{ player_name, seat_index }` |
| `player_left` | 有玩家离开 | `{ player_name, reason }` |
| `action_taken` | 玩家行动结果 | `{ player, action, amount, chips_remaining, valid }` |
| `game_event` | 阶段变更/发牌等 | `{ event: "phase_change\|deal_card\|pot_win\|showdown", detail }` |
| `hand_result` | 本手牌结算 | `{ winners: [{ player, hand_type, won_amount }] }` |
| `error` | 错误响应 | `{ code, message, ref_seq }` |
| `heartbeat_ack` | 心跳响应 | `{ server_time }` |

### 3.4 Error Code 规范

| Code | 含义 | 场景 |
|------|------|------|
| 1000 | 协议错误 | JSON 解析失败 |
| 1001 | 无效行动 | 下注额不合法、不在turn行动等 |
| 1002 | 桌已满 | table 座位已满 |
| 1003 | 筹码不足 | buy_in 不够最小买入 |
| 1004 | 重复坐下 | 已在座位上 |
| 1005 | 桌不存在 | 无效 table_id |
| 2000 | 内部错误 | 服务端异常 |

## 4. 通信拓扑

```
                   ┌──────────────┐
  Browser ──WS──▶  │  WS Gateway  │  (uWebSockets)
                   └──────┬───────┘
                          │ 内部消息 (in-process 或 Redis pub/sub)
                          ▼
                   ┌──────────────┐
                   │ Game Server  │  (phase13)
                   │  ┌────────┐  │
                   │  │ Table  │──│──▶ Game Engine (phase12)
                   │  └────────┘  │
                   │  ┌────────┐  │
                   │  │ AI Bot │──│──▶ AI Engine (phase11/13)
                   │  └────────┘  │
                   └──────────────┘
                          │
                   ┌──────▼───────┐
                   │  Persistence │  (phase14, SQLite/PG)
                   └──────────────┘
```

**单桌并发模型**：每张 Table 一个消息队列（串行），WS 收到的 player_action 入队，Table 按顺序消费。避免并发竞态。

## 5. 序列示例：完整一手牌

```
Client A: join_table { table_id: "t1", player_name: "Alice" }
Server:   player_joined { player: "Alice", seat: 0 }
Server:   table_state { ... table snapshot ... }

Client B: join_table { table_id: "t1", player_name: "Bob" }
Server:   player_joined { player: "Bob", seat: 1 }
Server:   table_state { ... }

(发牌)
Server:   game_event { event: "deal_card", detail: { player: "Alice", cards: ["Ah","Ks"] } }
Server:   game_event { event: "deal_card", detail: { player: "Bob", cards: ["??","??"] } }
Server:   table_state { current_player: "Alice", action_on: "Alice", phase: "preflop" }

Alice:    player_action { action: "raise", amount: 20 }
Server:   action_taken { player: "Alice", action: "raise", amount: 20 }
Server:   table_state { current_player: "Bob" }

Bob:      player_action { action: "call", amount: 20 }
Server:   action_taken { player: "Bob", action: "call", amount: 20 }
Server:   game_event { event: "phase_change", detail: { from: "preflop", to: "flop" } }

(翻牌)
Server:   game_event { event: "deal_card", detail: { cards: ["7d","Jc","3h"] } }
... (继续至 showdown)

Server:   hand_result { winners: [{ player: "Alice", hand: "One Pair", won: 40 }] }
Server:   game_event { event: "phase_change", detail: { from: "showdown", to: "hand_over" } }
```

## 6. 待决问题

- [ ] 多桌场景：WS 连接与 Table 实例的映射策略（in-process map vs Redis）
- [ ] 断线重连：session token 机制 + 状态同步
- [ ] 心跳间隔：建议 30s，超时 60s 自动 fold
- [ ] 二进制协议备选（Protobuf）是否需要——取决于性能压测结果

## 7. 关联 ADR

- ADR-001（架构总览）：Network Layer 部分
- ADR-003（游戏状态机）：消息触发的状态迁移
