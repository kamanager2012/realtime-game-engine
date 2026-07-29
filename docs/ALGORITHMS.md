# poker-engine 算法清单

> 最后核实：2026-06-26（对照 `poker_ws_server` + 全仓库源码）

本文档区分三层：

| 标记 | 含义 |
|------|------|
| **🟢 LIVE** | 线上 `poker_ws_server`（:9001）主链路在用 |
| **🟡 CODE** | 代码已实现，仅在 CLI/测试/锦标赛 API/离线工具中使用 |
| **🔴 STUB** | 框架或文档提及，未找到完整实现 |

---

## 0. 线上核心（真正撑起可玩牌桌）

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **自研 5-card Eval** | 花色独立 rank 统计 → 牌型分类 | `core/src/evaluator.cpp` | 🟢 LIVE |
| **7-card 枚举评估** | C(7,5)=21 组合取最优 | `core/src/evaluator.cpp` | 🟢 LIVE |
| **Monte Carlo Equity（Bot）** | 随机补全公共牌估算胜率；MEDIUM 约 **350 次**采样 | `equity/` + `phase13/ai_engine.cpp` | 🟢 LIVE |
| **规则 Bot AI** | equity + pot odds + aggression + bluff 启发式 | `phase13/ai_engine.cpp` | 🟢 LIVE（默认） |
| **CFR Bot AI** | 加载 `.cfr` 模型，按 infoset 策略采样行动 | `phase13/cfr_policy_store.cpp` | 🟢 LIVE（需 `POKER_CFR_MODEL_PATH`） |
| **Fisher-Yates 洗牌** | O(n) 均匀洗牌 | `phase12/dealer.cpp` | 🟢 LIVE |
| **NL Hold'em 规则引擎** | 发牌、下注轮、底池/边池、摊牌结算 | `phase12/` | 🟢 LIVE |
| **PBKDF2 密码哈希** | OpenSSL PKCS5_PBKDF2_HMAC，**10000 轮**，SHA-256 | `phase13/auth_service.cpp` | 🟢 LIVE |
| **ChipLedger 钱包** | buy-in 扣款 / cash-out 入账 / SQLite + Postgres 镜像 | `phase14/` | 🟢 LIVE |

**真人玩家无自动决策算法**。Bot 默认规则 AI；设置 `POKER_CFR_MODEL_PATH=/path/model.cfr` 后 Bot 走 **CFR 策略**（查表失败时回退规则 AI）。

---

## 一、手牌评估（Evaluator）

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **QuickEval（自研 5-card eval）** | rank 统计 → 同花/顺子/四条/葫芦等 | `core/src/evaluator.cpp`（线上路径） | 🟢 LIVE |
| **QuickEval（phase19 变体）** | 独立 HandEvaluator 实现 | `phase19/hand_evaluator.cpp` | 🟡 CODE |
| **7-card 枚举评估** | C(7,5)=21 取最优 | `core/src/evaluator.cpp` | 🟢 LIVE |
| **TwoCardHash** | 2 张手牌哈希，区分 suited/offsuit | `phase19/hand_evaluator.cpp` | 🟡 CODE |
| **PokerHandEvaluator（第三方）** | 标准 5/6/7 张评估参考实现 | `third_party/PokerHandEvaluator/` | 🟡 CODE（参考/测试） |
| **手牌 strength 编码** | category + rank 位压缩，支持直接比较 | `core/include/.../evaluator.h` | 🟢 LIVE |
| **Monte Carlo Equity** | 随机模拟剩余公共牌；**求解器默认 2000 次**，Bot 线上约 350 次 | `equity/equity_calculator.cpp` | 🟢 LIVE（Bot）/ 🟡 CODE（solver 默认 2000） |
| **Exact Equity** | 枚举精确胜率（小范围） | `equity/equity_calculator.cpp` | 🟡 CODE |

> ⚠️ 自研 QuickEval **未经过 2,598,960 全量对照验证**。建议与 cactus_kev / PokerHandEvaluator 做 golden test。

---

## 二、CFR / 博弈论求解

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **Vanilla CFR** | 完整遍历 + 后悔值更新 | `phase7/cfr_plus_solver.cpp`, `phase15/cfr_engine.cpp` | 🟡 CODE |
| **Chance-Sampled CFR** | 机会采样减少遍历量 | `phase7`（`CFRMode::CHANCE_SAMPLED`） | 🟡 CODE |
| **External Sampling CFR** | 外部采样（`CFR_External`） | `phase7/cfr_plus_solver.cpp` | 🟡 CODE |
| **Discounted CFR (DCFR)** | 遗憾/策略折扣（α/β） | `phase7`, `phase15/cfr_engine.cpp` | 🟡 CODE |
| **Regret Matching** | 从累计遗憾计算当前策略 | `phase15/cfr_node.h` | 🟡 CODE |
| **RM+（Regret Matching+）** | 负遗憾截断为 0 | `phase15`（`use_regret_matching_plus`） | 🟡 CODE |
| **Compact Node Store** | 64B/节点，regret 量化 | `phase20/compact_node.h` | 🟡 CODE |
| **DiskBacked Node Store** | 内存+磁盘混合，LRU 淘汰 | `phase20/disk_backed_store.cpp` | 🟡 CODE |
| **策略网络（MLP）** | 2 层全连接；含 Forward + TrainStep | `phase20/policy_network.cpp` | 🟡 CODE |
| **Node 哈希（FNV-1a）** | 开放寻址 infoset 表 | `phase15/cfr_engine.cpp` | 🟡 CODE |
| **Public Tree Solver** | 公共博弈树求解 | `phase15/public_tree_solver.cpp` | 🟡 CODE |
| **Parallel CFR** | 并行训练 | `phase15/parallel_cfr.cpp` | 🟡 CODE |

> ⚠️ CFR 可跑，但 **exploitability 收敛未做生产级验收**；**未接入 Bot 出牌**。  
> `poker_ws_server` 链接 `phase15` 主要用于 **TournamentServer HTTP API**，不是 live Bot 决策。

---

## 三、蒙特卡洛模拟

| 用途 | 说明 | 默认采样 | 状态 |
|------|------|----------|------|
| **Equity 计算（Bot）** | hero vs 全范围 | ~350（MEDIUM） | 🟢 LIVE |
| **Equity 计算（Solver/LUT）** | preflop/postflop 求解 | 2000 | 🟡 CODE |
| **CFR 采样遍历** | Chance/External Sampling | 依配置 | 🟡 CODE |
| **phase19 HandEvaluator MC** | 独立 2000 次模拟 | 2000 | 🟡 CODE |

---

## 四、AI / 优化算法

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **规则 Bot AI（MC + heuristic）** | 线上 Bot 唯一决策路径 | `phase13/ai_engine.cpp` | 🟢 LIVE |
| **贝叶斯优化（GP）** | Matern 核 + EI/UCB/PI，CFR 超参调优 | hyperparam 模块 + 测试 | 🟡 CODE |
| **Sobol 敏感性分析** | Saltelli 扩展方案 | hyperparam 测试 | 🟡 CODE |
| **策略网络（Policy Network）** | 2 层 MLP，ReLU，SGD TrainStep | `phase20/policy_network.cpp` | 🟡 CODE |
| **价值网络（Value Network）** | CFR 叶子剪枝用 | — | 🔴 STUB（未找到独立实现） |

> `AIConfig.use_lut = true` 存在于接口，但 **`ai_engine.cpp` 当前未读取该字段**。

---

## 五、锦标赛算法

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **瑞士轮配对** | Buchholz 破同分 + 避免重复配对 | `phase18/balancer.cpp` | 🟡 CODE（Tournament HTTP API） |
| **座位分配（HungarianAlgorithm）** | 代价矩阵最小匹配（函数名如此，是否标准 KM 待验） | `phase18/balancer.cpp` | 🟡 CODE |
| **断线替补** | 等候名单 + 综合评分 | `phase15/tournament.cpp` | 🟡 CODE |
| **动态平衡器** | AI 填充 / 盲注调整 / 桌子合并 | `phase18/balancer.cpp` | 🟡 CODE |
| **级别递增** | 定时上调盲注 | `phase15/tournament.cpp` | 🟡 CODE |
| **Elo-like 评分** | K 因子 + 底池加权 + 衰减 | Elo 模块 + 测试 | 🟡 CODE |

---

## 六、行为分析与反作弊

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **响应时间分析** | 均值/方差/分布，检测恒定延迟 | `phase21/behavior_analyzer.cpp` | 🟡 CODE |
| **投注一致性分析** | 下注比例方差 | `phase16/anticheat.cpp` | 🟡 CODE |
| **策略镜像检测** | 皮尔逊相关比较行为序列 | `phase16/anticheat.cpp` | 🟡 CODE |
| **时间相关性分析** | 行动时间同步性 | `phase16/anticheat.cpp` | 🟡 CODE |
| **合谋检测（Collusion）** | 同桌/邻座统计 | `phase16/anticheat.cpp` | 🟡 CODE |
| **Random Forest Bot 检测** | 28 维特征 + 自研 RF/决策树（**非 XGBoost**） | `phase17/ml_engine.cpp` | 🟡 CODE |
| **IP 信誉评分** | 黑白名单 + 威胁等级 | `phase21/security_policy_engine.cpp` | 🟡 CODE |
| **设备指纹** | Canvas Hash + UA + OS 组合 | security 模块 | 🟡 CODE |
| **多账户共享检测** | 同设备/同 IP 关联 | `phase16/` | 🟡 CODE |
| **Z-Score 异常检测** | 统计偏离度 | `phase21/behavior_analyzer.cpp` | 🟡 CODE |

> **反作弊模块未链接进 `poker_ws_server`**（`cli/CMakeLists.txt` 无 phase16/17/21）。

---

## 七、加密与安全

| 算法 | 说明 | 代码 | 状态 |
|------|------|------|------|
| **AES-256-GCM** | OpenSSL 对称加密 | `phase21/crypto_utils.cpp` | 🟡 CODE |
| **HMAC-SHA256** | 消息认证，时序安全比较 | `phase21/crypto_utils.cpp` | 🟡 CODE |
| **PBKDF2** | 密钥/密码派生；**线上 auth 10000 轮**；工具链另有 100K 配置 | `phase13/auth_service.cpp`, `phase21/` | 🟢 LIVE（10000 轮）/ 🟡 CODE（100K） |
| **SHA-256** | 通用哈希 | OpenSSL | 🟡 CODE |
| **Base64** | 标准编解码 | crypto 工具 | 🟡 CODE |
| **FNV-1a** | 快速非加密哈希 | CFR / 事件校验 | 🟡 CODE |
| **CSPRNG 令牌** | 32 字节 → hex token | `phase13/auth_service.cpp` | 🟢 LIVE |
| **mTLS 双向认证** | OpenSSL + CRL | `phase21/mtls_service.cpp` | 🟡 CODE（未启用） |

---

## 八、数据结构与存储

| 算法/结构 | 说明 | 代码 | 状态 |
|-----------|------|------|------|
| **FlatBuffers 序列化** | 零拷贝二进制序列化 | `phase20/flatbuffers_serializer.cpp` | 🟡 CODE（WS 仍用 JSON） |
| **Memory Arena** | 64B 缓存行对齐批量分配 | core/base | 🟡 CODE |
| **Lock-Free 队列（SPSC/MPSC）** | CAS 无锁 | core/base | 🟡 CODE |
| **Memory Pool** | 固定大小对象池 | core/base | 🟡 CODE |
| **Compact Node 编码** | int 量化 regret/strategy | `phase20/compact_node.h` | 🟡 CODE |
| **LRU 淘汰** | DiskBackedStore | `phase20/disk_backed_store.cpp` | 🟡 CODE |
| **SQLite WAL** | 账户/ledger 持久化 | `phase14/` | 🟢 LIVE |
| **Postgres 镜像** | ChipLedger 异步 mirror | `phase14/postgres_mirror.cpp` | 🟢 LIVE（配置 URL 时） |
| **事件溯源（WAL）** | 只追加二进制日志 | replay 模块 | 🟡 CODE |

---

## 九、洗牌与随机

| 算法 | 说明 | 状态 |
|------|------|------|
| **Fisher-Yates 洗牌** | O(n) 均匀排列 | 🟢 LIVE |
| **CSPRNG / mt19937** | 系统随机 + 引擎内 RNG | 🟢 LIVE |
| **牌编码** | `index = rank + suit×13` | 🟢 LIVE |

---

## 十、评分系统

| 算法 | 说明 | 状态 |
|------|------|------|
| **Elo-like 评分** | K 因子、底池加权 | 🟡 CODE |
| **K 因子衰减** | 局数越多 K 越小 | 🟡 CODE |

---

## 汇总统计

```
仓库内算法/结构：~40+ 种

🟢 LIVE（线上 WS 主链路）：   ~8 种
🟡 CODE（已实现未接 Bot）：   ~35 种
🔴 STUB（未证实）：           ~1 种（Value Network）

按领域：
  核心扑克（Eval/Equity/洗牌/引擎/Bot）：  6 种 LIVE + 若干 CODE
  CFR/博弈论：                             12 种 CODE
  AI 优化（BO/Sobol/PolicyNet）：          4 种 CODE
  锦标赛：                                 6 种 CODE
  反作弊：                                 10 种 CODE（未接 WS）
  加密安全：                               8 种（PBKDF2+token LIVE，其余 CODE）
  数据结构/存储：                          9 种（SQLite/Postgres LIVE）
```

---

## 关键修正记录（相对早期内部清单）

| 原描述 | 核实后 |
|--------|--------|
| MC Equity 默认 2000 次 | 求解器/LUT 默认 2000；**线上 Bot MEDIUM ≈ 350** |
| XGBoost Bot 检测 | 实为 **Random Forest + 决策树**（`phase17`） |
| 策略网络仅 forward | 含 **TrainStep（SGD）**，未接 live |
| Value Network 框架存在 | **未找到独立实现** → STUB |
| PBKDF2 100K 迭代 | **线上 auth 为 10000 轮** |
| CFR 已用于 Bot | **否**；Bot 仅用 MC + 规则 AI |
| FlatBuffers disabled | 已实现，**WS 协议仍 JSON** |
| 反作弊已上线 | **未链接 poker_ws_server** |

---

## CFR Bot 启用步骤

```bash
# 1. 生成默认策略表（bootstrap，约 5–10 秒，~48MB）
./scripts/train_cfr_bot_model.sh

# 2. 启动（dev.sh 会自动生成并 export）
./scripts/dev.sh

# 可选：实验性真实 CFR 训练（较慢，可能不稳定）
POKER_CFR_TRAIN_MODE=train POKER_CFR_ITERATIONS=200 ./scripts/train_cfr_bot_model.sh
```

## 相关文档

- [ADR-004 AI Engine 接口](adr/004-ai-engine-interface.md) — 设计目标：rule_based → cfr_model 渐进替换
- [PROGRESS.md](PROGRESS.md) — 阶段进度
- 线上 Bot 入口：`phase13/src/game_server.cpp` → `ProcessBotActions` → `AIEngine::MakeDecision`
- 线上 Eval/Equity：`core/src/evaluator.cpp`, `equity/src/equity_calculator.cpp`

---

## 一句话

**算法确实多，但分散在太多方向。** 砍掉与 live 牌桌无关的（BO/Sobol/反作弊/大部分 CFR 变体），当前真正需要精耕的不到 **10 种**；其中 **8 种已在 WS 主链路运行**。
