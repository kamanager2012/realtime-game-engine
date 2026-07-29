# 6-max 德州扑克 Solver — 开发计划

> 版本: v1.0
> 创建: 2026-06-13
> 状态: Phase 0 进行中

---

## 总览

```
Phase   名称                    里程碑                    预估工期
─────── ───────────────────── ──────────────────────── ─────────
  0     Core Evaluator+Range   评估+范围引擎可验证运行     2–3 周
  1     Replay System          用户可回放手牌并获 EV 分析  3–4 周
  2     Postflop Blueprint     翻后蓝图生成完毕             6–10 周
  3     Realtime Solver        在线对局实时求解             4–6 周
  4     Preflop Solver         翻前蓝图                     8–12 周
  5     AI Player              可在线对抗的 Bot             4–6 周
  6     Platform               在线万人平台                 8–12 周
```

---

## Phase 0 详细计划

```
目标:  完整的 7-card 评估引擎 + 1326 范围系统 + 公平计算
产出:
  - Evaluator: 纳秒级 5-card / 微秒级 7-card
  - Range: 1326 编码, 范围解析, 过滤, 归一化, 花色同构
  - EquityCalculator: 精确 + Monte Carlo 双模式
  - 完整单元测试 + 基准测试 (Google Test)
  - CMake 构建, 可编译为静态库

文件规模:  ~4,000 行代码 + 1,500 行测试/基准
```

## Phase 1 详细计划

```
目标:  手牌回放 + EV 分析系统
新增:
  - Hand History Parser (标准格式: HH / PokerStars / GG)
  - Node-by-node EV 回溯
  - 错误检测 + 泄漏报告
  - 数据持久化 (SQLite / ClickHouse)
  - 轻量前端 (Web / CLI)

产品化里程碑: 第一个可销售的版本
```

## Phase 2 详细计划

```
目标:  翻后蓝图生成器
新增:
  - 博弈树构建器 (含抽象)
  - 抽象引擎 (Card Abstraction / Action Abstraction)
  - CFR+ / DCFR 求解器 (支持 MCCFR 采样)
  - 可插拔求解器接口 (CFR / CFR+ / MCCFR / DCFR)
  - Strategy Store (mmap + LRU 缓存 + 版本化管理)
  - 多线程 + 断点续训
  - 收敛监控 (Exploitability 曲线)

产出:  100% 翻后蓝图 (约 500 GB–2 TB 数据)
```

## Phase 3 详细计划

```
目标:  在线实时求解
新增:
  - Blueprint Loader + 内存管理
  - Subgame Solving (Safe Subgame)
  - 局部重求解引擎 (< 200ms 延迟)
  - 策略插值
  - 实时决策 API
  - 低延迟网络层 (gRPC)

技术风险:  Imitation Gap (Farina et al. 2020)
          → 采用 Nash Anchor + Reach-based 方法
```

## Phase 4 详细计划

```
目标:  翻前蓝图
新增:
  - 翻前博弈树 (约 10^6 节点)
  - 更强的 Card Abstraction (169 → 更粗粒度)
  - 大规模分布式训练集群
  - 存储: ~50–200 GB
  - 训练: ~1–4 周 (256 核集群)

资源需求峰值阶段
```

## Phase 5 详细计划

```
目标:  AI 玩家 (Bot)
新增:
  - 实时策略网络 (Policy + Value)
  - 蒸馏 (Blueprint → Compact Network)
  - Action Translation (蓝图 → 实际动作)
  - 反检测系统 (人类行为模拟)
  - 自对弈持续改进
```

## Phase 6 详细计划

```
目标:  在线平台
新增:
  - 匹配系统 (ELO / Skill-based)
  - 房间管理 / 桌管理
  - 反作弊 / 反 Solver 检测
  - 用户系统 / 钱包 / 统计
  - 合规与牌照
  - 高可用集群

需要所有前面阶段的完整交付
```

---

## 关键参考文档

- `../poker-ai/01-cfr-algorithm-and-system-design.md` — CFR 算法基础
- `../poker-ai/02-subgame-solving-deep-dive.md` — Subgame Solving 理论
- `../poker-ai/03-preflop-blueprint-and-realtime-optimization.md` — Preflop/实时求解
- `../poker-ai/04-cfr-advances-2025-supplement.md` — 2025 最新算法
- `../poker-ai/05-rlcard-code-analysis.md` — RLCard 工程参考
- `../poker-ai/06-deepcfr-poker-analysis.md` — DeepCFR 6-max 参考
