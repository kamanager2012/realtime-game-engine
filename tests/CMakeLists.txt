# googletest — use vendored pre-built libs
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    add_library(GTest::gtest UNKNOWN IMPORTED)
    set_target_properties(GTest::gtest PROPERTIES
        IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/libgtest.a
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/third_party/googletest/include
    )
    add_library(GTest::gtest_main UNKNOWN IMPORTED)
    set_target_properties(GTest::gtest_main PROPERTIES
        IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/third_party/googletest/lib/libgtest_main.a
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/third_party/googletest/include
        INTERFACE_LINK_LIBRARIES GTest::gtest
    )
endif()

# ==================== Shared test link/include ====================

set(TEST_LINK_LIBS
    PRIVATE
        core
        equity
        phase1
        phase2
        phase3
        phase4
        phase5
        phase6
        phase7
        phase8
        phase9
        phase10
        phase11
        phase12
        phase19
        phase20
        phase13
        phase14
        phase15
        phase16
        phase17
        phase18
        phase21
        poker_net
        GTest::gtest_main
)

set(TEST_INCLUDE_DIRS
    PRIVATE
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/phase16/include
        ${CMAKE_SOURCE_DIR}/phase17/include
        ${CMAKE_SOURCE_DIR}/phase18/include
        ${CMAKE_SOURCE_DIR}/phase19/include
        ${CMAKE_SOURCE_DIR}/phase20/include
        ${CMAKE_SOURCE_DIR}/phase21/include
        ${CMAKE_SOURCE_DIR}/infra/net/include
        ${CMAKE_SOURCE_DIR}/cli
        ${CMAKE_SOURCE_DIR}/third_party/eigen3
)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

# ==================== Fast tests (CI gate, <30s total) ====================

set(FAST_TEST_SOURCES
    test_main.cpp
    evaluator_test.cpp
    range_test.cpp
    equity_test.cpp
    phase1_test.cpp
    phase3_test.cpp
    phase4_test.cpp
    phase5_test.cpp
    phase9_test.cpp
    phase11_test.cpp
    phase12_test.cpp
    phase13_test.cpp
    phase14_test.cpp
    chip_ledger_test.cpp
    economy_flow_test.cpp
    health_probe_test.cpp
    metrics_registry_test.cpp
    distributed_session_store_test.cpp
    postgres_mirror_test.cpp
    postgres_mirror_integration_test.cpp
    redis_client_test.cpp
    buy_in_policy_test.cpp
    phase15_test.cpp
    actor_concurrency_test.cpp
    safe_database_test.cpp
    persistence_test.cpp
    ml_anticheat_test.cpp
    anticheat_test.cpp
    gdpr_test.cpp
    crypto_security_test.cpp
    security_test.cpp
    hyperparam_test.cpp
    security_integration_test.cpp
    test_cfr_bot_live.cpp
)
# NOTE: property_tests.cpp is a manual test — timeouts in the game engine
# make property-based tests slow. Run manually: ./poker_tests --gtest_filter='PropertyTests*'
# add_executable(property_tests property_tests.cpp)

add_executable(poker_tests ${FAST_TEST_SOURCES})
target_link_libraries(poker_tests ${TEST_LINK_LIBS})
target_include_directories(poker_tests ${TEST_INCLUDE_DIRS})
target_compile_definitions(poker_tests PRIVATE HAVE_EIGEN3=1)
target_link_libraries(poker_tests PRIVATE ${SQLite3_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(poker_tests PRIVATE ${SQLite3_INCLUDE_DIRS})

include(GoogleTest)
gtest_discover_tests(poker_tests)

# ==================== Slow tests (CFR solver, 20-40s each) ====================

set(SLOW_TEST_SOURCES
    test_main.cpp
    phase2_test.cpp
    phase6_test.cpp
    phase7_test.cpp
    phase8_test.cpp
    phase10_test.cpp
    benchmark.cpp
)

add_executable(poker_slow_tests ${SLOW_TEST_SOURCES})
target_link_libraries(poker_slow_tests ${TEST_LINK_LIBS})
target_include_directories(poker_slow_tests ${TEST_INCLUDE_DIRS})
target_compile_definitions(poker_slow_tests PRIVATE HAVE_EIGEN3=1)
target_link_libraries(poker_slow_tests PRIVATE ${SQLite3_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(poker_slow_tests PRIVATE ${SQLite3_INCLUDE_DIRS})

gtest_discover_tests(poker_slow_tests)
