#if 0  // depends on phase17 (spectator) which is not built - needs phase17 first
#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "poker_engine/anticheat/ml_data_generator.h"
#include "poker_engine/anticheat/ml_engine.h"
#include "poker_engine/base/audit_logger.h"
#include "poker_engine/base/structured_audit_logger.h"
#include "poker_engine/base/structured_logger.h"
#include "poker_engine/cfr/cfr_engine.h"
#include "poker_engine/network/session_manager.h"
#include "poker_engine/network/websocket_server.h"
#include "poker_engine/security/auth_manager.h"
#include "poker_engine/security/security_policy_engine.h"
#include "poker_engine/spectator/spectator_manager.h"
#include "poker_engine/spectator/spectator_ws.h"

using namespace poker_engine;

class EndToEndTest : public ::testing::Test {
protected:
    std::unique_ptr<network::WebSocketServer> ws_server_;
    std::unique_ptr<spectator::SpectatorManager> spec_mgr_;
    std::unique_ptr<spectator::SpectatorWSServer> spec_ws_;
    std::unique_ptr<security::SecurityPolicyEngine> security_engine_;
    std::unique_ptr<anticheat::MLEntityDetector> ml_detector_;

    void SetUp() override {
        // 初始化结构化日志
        base::StructuredLogger::Initialize("/tmp/poker_test/e2e_logs");

        // 初始化安全引擎
        security_engine_ = std::make_unique<security::SecurityPolicyEngine>();
        security_engine_->Initialize();

        // 初始化 ML 检测器
        ml_detector_ = std::make_unique<anticheat::MLEntityDetector>();
        anticheat::MLDataGenerator gen(42);
        auto dataset = gen.GenerateDataset(500, 100, 100, 50);
        for (auto& [fv, label] : dataset) {
            ml_detector_->AddTrainingSample(fv, label);
        }
        ml_detector_->Train();

        // 初始化观战系统
        spec_mgr_ = std::make_unique<spectator::SpectatorManager>();
        spec_ws_ = std::make_unique<spectator::SpectatorWSServer>(
            19999, /* session_mgr */ *reinterpret_cast<network::SessionManager*>(nullptr),
            *spec_mgr_);

        LOG_INFO("End-to-end test environment initialized");
    }

    void TearDown() override {
        spec_ws_.reset();
        spec_mgr_.reset();
        security_engine_.reset();
        ml_detector_.reset();
        base::StructuredLogger::Shutdown();
        base::AuditLogger::Instance().Shutdown();
    }
};

// ==================== 测试 1: 完整 ML 管线 ====================

TEST_F(EndToEndTest, MLTrainingPipeline) {
    ASSERT_TRUE(ml_detector_->IsTrained());
    EXPECT_GT(ml_detector_->TreeCount(), 0u);

    // 测试已知正常玩家
    anticheat::PlayerStatistics normal;
    normal.player_id = 1;
    normal.hands_played = 200;
    normal.vpip_pct = 25.0;
    normal.pfr_pct = 20.0;
    normal.agg_factor = 1.5;
    normal.bet_sizing_history.resize(200, 0.65);
    normal.response_times_ms.resize(200, 2000);

    // 添加 Positional Stats
    normal.early.hands = 50; normal.early.vpip = 15;
    normal.middle.hands = 50; normal.middle.vpip = 20;
    normal.late.hands = 50; normal.late.vpip = 30;
    normal.blind.hands = 50; normal.blind.vpip = 35;

    auto normal_result = ml_detector_->Analyze(normal);
    LOG_INFO("Normal player analysis: risk={:.1f}, level={}",
             normal_result.overall_risk_score,
             static_cast<int>(normal_result.suspicion_level));

    // 测试已知 Bot
    anticheat::PlayerStatistics bot;
    bot.player_id = 2;
    bot.hands_played = 500;
    bot.vpip_pct = 28.0;
    bot.pfr_pct = 27.0;
    bot.agg_factor = 1.2;
    bot.response_times_ms.resize(500, 2000);  // 恒定 2s
    bot.bet_sizing_history.resize(500, 0.68);

    bot.early.hands = 125; bot.early.vpip = 35;
    bot.middle.hands = 125; bot.middle.vpip = 35;
    bot.late.hands = 125; bot.late.vpip = 35;
    bot.blind.hands = 125; bot.blind.vpip = 35;

    auto bot_result = ml_detector_->Analyze(bot);
    LOG_INFO("Bot analysis: risk={:.1f}, level={}",
             bot_result.overall_risk_score,
             static_cast<int>(bot_result.suspicion_level));
}

// ==================== 测试 2: 安全评估管线 ====================

TEST_F(EndToEndTest, SecurityAssessment) {
    // 模拟玩家连接
    security::DeviceFingerprint fp;
    fp.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)";
    fp.os = "Windows";
    fp.browser = "Chrome/120.0";
    fp.canvas_hash = "abc123def456";
    fp.fingerprint_hash = fp.ComputeHash();

    auto assessment = security_engine_->EvaluateConnection(
        "192.168.1.100", fp, nullptr);

    EXPECT_GE(assessment.overall_score, 0.0);
    EXPECT_LE(assessment.overall_score, 100.0);

    auto decision = security_engine_->MakeDecision(assessment);

    // 内部 IP 应被允许
    EXPECT_EQ(decision.action, security::SecurityPolicyEngine::ActionType::Allow);
}

// ==================== 测试 3: 审计日志完整性 ====================

TEST_F(EndToEndTest, AuditLogCompleteness) {
    auto& audit_logger = base::AuditLogger::Instance();
    audit_logger.Initialize("/tmp/poker_test/audit.eaudit");

    // 记录各种事件
    AUDIT_LOG(AuditEvent::Login, 1001, "", "{\"ip\":\"127.0.0.1\"}");
    AUDIT_LOG(AuditEvent::JoinTable, 1001, "table_1", "{}");
    AUDIT_LOG(AuditEvent::ActionTaken, 1001, "table_1",
              "{\"action\":\"raise\",\"amount\":500}");
    AUDIT_LOG(AuditEvent::Logout, 1001, "", "{}");

    auto results = audit_logger.QueryByPlayer(1001, 10);
    EXPECT_GE(results.size(), 4u);

    audit_logger.Shutdown();
}

// ==================== 测试 4: 并发安全评估 ====================

TEST_F(EndToEndTest, ConcurrentSecurityEvaluation) {
    const int NUM_THREADS = 8;
    const int EVALS_PER_THREAD = 50;
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < EVALS_PER_THREAD; ++i) {
                int player_id = t * EVALS_PER_THREAD + i;

                security::DeviceFingerprint fp;
                fp.user_agent = "Test UA " + std::to_string(player_id);
                fp.fingerprint_hash = std::to_string(player_id);

                anticheat::PlayerStatistics stats;
                stats.player_id = player_id;
                stats.hands_played = 100;
                stats.vpip_pct = 25.0;
                stats.pfr_pct = 22.0;
                stats.response_times_ms.resize(100, 2000 + player_id);
                stats.bet_sizing_history.resize(100, 0.5);

                auto assessment = security_engine_->EvaluateConnection(
                    "10.0." + std::to_string(t) + "." + std::to_string(i),
                    fp, &stats);

                if (assessment.overall_score >= 0 &&
                    assessment.overall_score <= 100) {
                    success_count++;
                } else {
                    fail_count++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(fail_count.load(), 0);
    EXPECT_EQ(success_count.load(), NUM_THREADS * EVALS_PER_THREAD);
    LOG_INFO("Concurrent security evaluation: {} passed, {} failed",
             success_count.load(), fail_count.load());
}

// ==================== 测试 5: GDPR 工作流 ====================

TEST_F(EndToEndTest, GDPRWorkflow) {
    GDPRComplianceEngine gdpr;
    gdpr.SetConfig({30, 365, true, true, "dpo@test.com"});

    int64_t player_id = 12345;

    // 注册同意
    gdpr.RecordConsent(player_id, DataCategory::PersonalIdentity,
                        true, "account_creation");
    gdpr.RecordConsent(player_id, DataCategory::Financial,
                        true, "payments");

    EXPECT_TRUE(gdpr.HasConsent(player_id, DataCategory::PersonalIdentity));

    // 访问请求
    auto access_result = gdpr.SubmitAccessRequest(player_id, player_id);
    EXPECT_TRUE(access_result.IsOk());

    // 数据可携
    auto portability = gdpr.ExportDataPortability(player_id);
    EXPECT_TRUE(portability.IsOk());

    // 撤回同意
    gdpr.WithdrawConsent(player_id);
    EXPECT_FALSE(gdpr.IsPlayerDataRestricted(player_id) == false);
    // 撤回后应该被限制
    EXPECT_TRUE(gdpr.IsPlayerDataRestricted(player_id));

    // 删除请求
    auto erasure = gdpr.SubmitErasureRequest(player_id, "No longer playing");
    EXPECT_TRUE(erasure.IsOk());
}

// ==================== 测试 6: 性能综合测试 ====================

TEST_F(EndToEndTest, OverallPerformanceBenchmark) {
    auto start = std::chrono::high_resolution_clock::now();

    const int num_players = 1000;

    // 模拟: 对每个玩家进行安全评估 + ML 分析
    for (int i = 0; i < num_players; ++i) {
        // 1. 安全评估
        security::DeviceFingerprint fp;
        fp.fingerprint_hash = "fp_" + std::to_string(i);

        auto assessment = security_engine_->EvaluateConnection(
            "192.168.1." + std::to_string(i % 256), fp, nullptr);
        auto decision = security_engine_->MakeDecision(assessment);

        // 2. ML 分析（使用已有模型）
        if (ml_detector_->IsTrained()) {
            anticheat::PlayerStatistics stats;
            stats.player_id = i;
            stats.hands_played = 100;
            stats.vpip_pct = 25.0;
            stats.pfr_pct = 22.0;
            stats.response_times_ms.resize(100, 2000);
            stats.bet_sizing_history.resize(100, 0.5);
            stats.early.hands = 25; stats.early.vpip = 10;
            stats.middle.hands = 25; stats.middle.vpip = 12;
            stats.late.hands = 25; stats.late.vpip = 14;
            stats.blind.hands = 25; stats.blind.vpip = 12;

            auto ml_result = ml_detector_->Analyze(stats);
            (void)ml_result;  // 避免未使用
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start).count();

    LOG_INFO("[Perf] E2E: {} players analyzed in {}ms ({} players/sec)",
             num_players, elapsed, num_players * 1000.0 / elapsed);

    // 性能要求: > 200 players/sec (含安全 + ML)
    double throughput = num_players * 1000.0 / elapsed;
    EXPECT_GT(throughput, 200.0);
}
#endif
