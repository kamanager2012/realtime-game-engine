set(PHASE20_SOURCES
    src/flatbuffers_serializer.cpp
    src/actor_lockfree.cpp
    src/disk_backed_store.cpp
    src/policy_network.cpp
)

set(PHASE20_HEADERS
    include/poker_engine/serialization/flatbuffers_serializer.h
    include/poker_engine/serialization/serialization_bench.h
    include/poker_engine/concurrency/lockfree_queue.h
    include/poker_engine/cfr/disk_backed_store.h
    include/poker_engine/cfr/compact_node.h
    include/poker_engine/cfr/policy_network.h
)

add_library(phase20 STATIC ${PHASE20_SOURCES} ${PHASE20_HEADERS})

target_include_directories(phase20
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/phase19/include
        ${CMAKE_SOURCE_DIR}/phase18/include
        ${CMAKE_SOURCE_DIR}/phase17/include
        ${CMAKE_SOURCE_DIR}/phase16/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/core/include
)

# Optional dependencies
find_package(Eigen3 QUIET)
find_package(nlohmann_json QUIET)
find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)

target_link_libraries(phase20
    PUBLIC
        poker_engine_core
        phase19
    PRIVATE
        Threads::Threads
)

# FlatBuffers — use vendored headers (header-only mode for serialization)
find_package(FlatBuffers QUIET)
if(FlatBuffers_FOUND)
    target_link_libraries(phase20 PRIVATE FlatBuffers::FlatBuffers)
    target_compile_definitions(phase20 PRIVATE HAVE_FLATBUFFERS=1)
else()
    # Vendored FlatBuffers headers (v24.3.25)
    target_include_directories(phase20 SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party
    )
    target_compile_definitions(phase20 PRIVATE HAVE_FLATBUFFERS=1)
endif()

if(Eigen3_FOUND)
    target_link_libraries(phase20 PRIVATE Eigen3::Eigen)
endif()

if(nlohmann_json_FOUND)
    target_link_libraries(phase20 PRIVATE nlohmann_json::nlohmann_json)
endif()

if(SQLite3_FOUND)
    target_link_libraries(phase20 PRIVATE SQLite::SQLite3)
endif()
