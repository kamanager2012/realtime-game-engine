set(ARENA_SOURCES
    src/random_agent.cpp
    src/baseline_agents.cpp
    src/match_runner.cpp
)

set(ARENA_HEADERS
    include/poker_engine/arena/random_agent.h
    include/poker_engine/arena/baseline_agents.h
    include/poker_engine/arena/match_runner.h
)

add_library(arena STATIC ${ARENA_SOURCES} ${ARENA_HEADERS})

target_include_directories(arena
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/equity/include
)

target_link_libraries(arena
    PUBLIC
        phase13
        phase12
        equity
    PRIVATE
        Threads::Threads
)
