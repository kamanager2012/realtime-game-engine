set(PHASE16_SOURCES
    src/replay_types.cpp
    src/replay_engine.cpp
    src/replay_ws.cpp
    src/anticheat.cpp
    src/anticheat_api.cpp
    src/admin_handler.cpp
    src/anticheat_integration.cpp
)

set(PHASE16_HEADERS
    include/poker_engine/replay/replay_types.h
    include/poker_engine/replay/replay_engine.h
    include/poker_engine/replay/replay_ws.h
    include/poker_engine/anticheat/anticheat.h
    include/poker_engine/anticheat/anticheat_api.h
    include/poker_engine/anticheat/admin_handler.h
    include/poker_engine/tournament/anticheat_integration.h
)

add_library(phase16 STATIC ${PHASE16_SOURCES} ${PHASE16_HEADERS})

target_include_directories(phase16
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/phase15/include
)

target_link_libraries(phase16
    PUBLIC
        poker_engine_core
        phase15
    PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
)
