
set(PHASE21_SOURCES
    src/vault_manager.cpp
    src/mtls_service.cpp
    src/gdpr_engine.cpp
    src/ip_reputation.cpp
    src/behavior_analyzer.cpp
    src/security_policy_engine.cpp
    src/crypto_utils.cpp
    src/websocket_mtls.cpp
    src/structured_audit_logger.cpp
    src/tracer.cpp
    src/table_actor_traced.cpp
    src/structured_logger.cpp
    src/secure_router.cpp
)

set(PHASE21_HEADERS
    include/poker_engine/security/vault_manager.h
    include/poker_engine/security/mtls_service.h
    include/poker_engine/security/gdpr_engine.h
    include/poker_engine/security/ip_reputation.h
    include/poker_engine/security/behavior_analyzer.h
    include/poker_engine/security/security_policy_engine.h
    include/poker_engine/security/crypto_utils.h
    include/poker_engine/security/secure_router.h
    include/poker_engine/base/structured_audit_logger.h
    include/poker_engine/base/structured_logger.h
    include/poker_engine/observability/tracer.h
)

add_library(phase21 STATIC ${PHASE21_SOURCES} ${PHASE21_HEADERS})

target_include_directories(phase21
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/phase19/include
        ${CMAKE_SOURCE_DIR}/phase20/include
        ${CMAKE_SOURCE_DIR}/phase18/include
        ${CMAKE_SOURCE_DIR}/phase16/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/core/include
)

# Dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json QUIET)
find_package(spdlog QUIET)

target_link_libraries(phase21
    PUBLIC
        poker_engine_core
        phase16
        phase19
        OpenSSL::Crypto
        OpenSSL::SSL
    PRIVATE
        Threads::Threads
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)
