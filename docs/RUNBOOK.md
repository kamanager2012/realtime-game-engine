# 本地端到端运行手册（SQLite-only，无 Docker/Redis/Postgres）

> 适用：开发/演示环境。生产部署见 `deploy/docker-compose.yml`（含 Redis/Postgres/metrics/nginx）。

## 1. 构建

```bash
# 后端（C++20，依赖 vendored，无需联网）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel            # 产出 build/cli/poker_ws_server

# 前端（React 18 + Vite 5）
cd frontend && npm install && npm run build   # 产出 frontend/dist
```

## 2. 启动服务（单二进制，9001 端口同时提供 WebSocket + REST + /metrics）

```bash
# SQLite-only：不设置 REDIS/POSTGRES 环境变量即自动跳过（仅打印 [WARN]）
POKER_DB_PATH=./poker_data.db ./build/cli/poker_ws_server
```

启动后自动：
- 执行 7 个 DB 迁移（含 v7 wallet ledger）
- 建默认锦标赛 `每日锦标赛`
- 建 `main` / `table_1` 两张桌子并各放 3 个 bot，自动开桌打牌

## 3. 健康检查与可观测性

```bash
curl localhost:9001/health     # {"status":"ok","mode":"development","db":true,...}
curl localhost:9001/metrics    # Prometheus 格式指标
```

## 4. REST API（同一 9001 端口）

| 端点 | 说明 |
|------|------|
| `GET /api/hands?limit=N` | 最近手牌列表 |
| `GET /api/hands/<id>` | 手牌详情（底牌/行动/最佳牌型） |
| `GET /api/players/<id>/stats` | 玩家统计 VPIP/PFR/AF/BB·100 |
| `GET /api/leaderboard?limit=N` | 排行榜 |
| `GET /api/tables` | 当前牌桌 |
| `GET /api/tournaments` | 锦标赛列表 |
| `GET /leaderboard` | 独立排行榜网页（HTML） |

## 5. 前端接入

浏览器通过 WebSocket 连接 `ws://<host>:9001/`（见 `frontend/src/utils/network.ts`）。
生产环境由 `deploy/nginx.conf` 在 9001 前做静态资源反代；本地开发可用 `frontend/vite.config.ts` 的 proxy。

## 6. 账本与审计

`chip_ledger` 是**不可变追加账本**：每笔 chip 变动写入 `wallet_transactions`
（含 `balance_after`），并在建号时写入一条 `grant` 起始余额，使账本自洽、可独立对账。

```bash
./build/cli/db_tool ledger-check <db>     # 校验 chip_ledger 与 accounts 余额一致
./build/cli/db_tool ledger-export out.csv <db>  # 导出不可变流水 (CSV)
./build/cli/db_tool health                # DB 健康检查
```

反作弊：`poker_ws_server` 每完成一手牌调用 `AntiCheatManager::SubmitHandData`，
每 N 手触发 `RunAnalysis()`，命中的可疑对子经 `StructuredAuditLogger` 写入
`audit_<ts>.audit`（JSON Lines，兼容 SIEM）。

## 7. 公平性

每手牌洗牌使用**密码学随机种子**（`std::random_device`，非固定常数）并发布
commit-reveal 承诺哈希（`Sha256(seed||nonce)`），整手牌为单一可验证洗牌。
结算后把 `HandProof{commitment, seed, nonce, deck_hash}` 写入手牌记录
（REST `GET /api/hands/<id>` 的 `rng_proof` 字段），审计方可重放验证。
见 `phase12/src/dealer.cpp` 的 `HandProof()` 与 `phase12/src/game_state.cpp`。
