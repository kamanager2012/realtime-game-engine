
set(PHASE19_SOURCES
    src/result.cpp
    src/safe_database.cpp
    src/player_repository_safe.cpp
    src/hand_evaluator.cpp
    src/audit_logger.cpp
    src/reconnection_manager.cpp
)

set(PHASE19_HEADERS
    include/poker_engine/base/result.h
    include/poker_engine/base/owned_ptr.h
    include/poker_engine/base/audit_logger.h
    include/poker_engine/base/memory_arena.h
    include/poker_engine/base/rate_limiter.h
    include/poker_engine/persistence/safe_query.h
    include/poker_engine/persistence/safe_database.h
    include/poker_engine/persistence/player_repository_safe.h
    include/poker_engine/evaluator/hand_evaluator.h
    include/poker_engine/network/reconnection_manager.h
    include/poker_engine/network/table_node.h
)

add_library(phase19 STATIC ${PHASE19_SOURCES} ${PHASE19_HEADERS})

target_include_directories(phase19
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase18/include
        ${CMAKE_SOURCE_DIR}/phase17/include
        ${CMAKE_SOURCE_DIR}/phase16/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/phase13/include
)

# Optional dependencies
find_package(Eigen3 QUIET)
find_package(nlohmann_json QUIET)
find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)

target_link_libraries(phase19
    PUBLIC
        poker_engine_core
    PRIVATE
        Threads::Threads
)

if(SQLite3_FOUND)
    target_link_libraries(phase19 PRIVATE SQLite::SQLite3)
    target_compile_definitions(phase19 PRIVATE HAVE_SQLITE3=1)
endif()

if(nlohmann_json_FOUND)
    target_link_libraries(phase19 PRIVATE nlohmann_json::nlohmann_json)
endif()

if(Eigen3_FOUND)
    target_link_libraries(phase19 PRIVATE Eigen3::Eigen)
endif()
