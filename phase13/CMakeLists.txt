set(PHASE13_SOURCES
    src/websocket_server.cpp
    src/ai_engine.cpp
    src/game_server.cpp
    src/auth_service.cpp
    src/cfr_policy_store.cpp
)

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json QUIET)

add_library(phase13 STATIC ${PHASE13_SOURCES})
target_include_directories(phase13
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/equity/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/core/include
)
target_link_libraries(phase13 PUBLIC phase12 phase14 phase15 core equity Threads::Threads OpenSSL::Crypto nlohmann_json::nlohmann_json)

# cpp-httplib (header-only WebSocket + HTTP server)
target_include_directories(phase13 PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(phase13 PRIVATE CPPHTTPLIB_USE_OPENSSL_IF_AVAILABLE)
