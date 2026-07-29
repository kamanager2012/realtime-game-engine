# =============================================================================
# Network module - Redis client + distributed session store.
# Deprecated WS server removed from build; see README.md for migration plan.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(NET_SOURCES
    src/redis_client.cpp
    src/distributed_session_store.cpp
)

set(NET_HEADERS
    include/poker_engine/network/redis_client.h
    include/poker_engine/network/distributed_session_store.h
    include/poker_engine/network/health_probe.h
)

add_library(poker_net STATIC ${NET_SOURCES} ${NET_HEADERS})

target_include_directories(poker_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase13/include
)

target_link_libraries(poker_net
    PUBLIC
        Threads::Threads
    PRIVATE
        poker_engine_core
        phase13
)

# OpenSSL (required for Redis TLS and RNG)
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    target_link_libraries(poker_net PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(poker_net PRIVATE HAVE_OPENSSL=1)
endif()

# DEPRECATED: WS server and associated targets removed from build.
# Source files retained for reference in src/ws_server.cpp, src/ws_session.cpp, etc.
# See README.md for migration plan.
