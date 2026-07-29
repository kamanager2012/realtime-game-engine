find_package(Threads REQUIRED)

set(PHASE10_SOURCES
    src/parallel_utils.cpp
    src/parallel_cfr.cpp
    src/parallel_preflop_lut.cpp
    src/parallel_batch_parser.cpp
    src/benchmark.cpp
)

add_library(phase10 STATIC ${PHASE10_SOURCES})
target_include_directories(phase10
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/equity/include
        ${CMAKE_SOURCE_DIR}/phase4/include
        ${CMAKE_SOURCE_DIR}/phase8/include
)
target_link_libraries(phase10 PUBLIC phase8 phase4 core equity Threads::Threads)

find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_compile_options(phase10 PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(phase10 PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP found - parallel execution enabled")
else()
    message(STATUS "OpenMP not found - using thread-based parallelism")
endif()
