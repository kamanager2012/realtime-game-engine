cmake_minimum_required(VERSION 3.16)

set(PHASE17_SOURCES
    src/spectator_manager.cpp
    src/spectator_ws.cpp
    src/tournament_broadcaster.cpp
    src/ml_engine.cpp
    src/ml_data_generator.cpp
)

set(PHASE17_HEADERS
    include/poker_engine/spectator/spectator_types.h
    include/poker_engine/spectator/spectator_manager.h
    include/poker_engine/spectator/spectator_ws.h
    include/poker_engine/spectator/tournament_broadcaster.h
    include/poker_engine/anticheat/ml_engine.h
    include/poker_engine/anticheat/ml_data_generator.h
)

add_library(phase17 STATIC ${PHASE17_SOURCES} ${PHASE17_HEADERS})

target_include_directories(phase17
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/phase16/include
        ${CMAKE_SOURCE_DIR}/infra/net/include
)

find_package(Threads REQUIRED)
target_link_libraries(phase17
    PUBLIC
        poker_engine_core
        phase15
        phase16
        poker_net
    PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
)
