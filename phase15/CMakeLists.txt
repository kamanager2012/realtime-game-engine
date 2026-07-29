set(PHASE15_SOURCES
    src/cfr_engine.cpp
    src/cfr_abstraction.cpp
    src/cfr_range.cpp
    src/cfr_model.cpp
    src/cfr_training.cpp
    src/parallel_cfr.cpp
    src/public_tree_solver.cpp
    src/gpu_batcher.cpp
    src/tournament.cpp
    src/tournament_server.cpp
)

set(PHASE15_HEADERS
    include/poker_engine/cfr/types.h
    include/poker_engine/cfr/cfr_engine.h
    include/poker_engine/cfr/cfr_node.h
    include/poker_engine/cfr/cfr_abstraction.h
    include/poker_engine/cfr/cfr_range.h
    include/poker_engine/cfr/cfr_training.h
    include/poker_engine/cfr/cfr_model.h
    include/poker_engine/cfr/parallel_cfr.h
    include/poker_engine/cfr/public_tree_solver.h
    include/poker_engine/cfr/gpu_batcher.h
    include/poker_engine/tournament/tournament.h
    include/poker_engine/tournament/tournament_server.h
)

add_library(phase15 STATIC ${PHASE15_SOURCES} ${PHASE15_HEADERS})

target_include_directories(phase15
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase13/include
)

target_link_libraries(phase15
    PUBLIC
        poker_engine_core
    PRIVATE
        nlohmann_json::nlohmann_json
        fmt::fmt
        Threads::Threads
)
