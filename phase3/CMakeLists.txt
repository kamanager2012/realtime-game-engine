set(PHASE3_SOURCES
    src/flop_explorer.cpp
    src/spot_solver.cpp
    src/hand_replayer.cpp
    src/batch_simulator.cpp
)

add_library(phase3 STATIC ${PHASE3_SOURCES})
target_include_directories(phase3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase1/include
        ${CMAKE_SOURCE_DIR}/phase2/include
)
target_link_libraries(phase3 PUBLIC core equity phase1 phase2)
