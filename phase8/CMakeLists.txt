set(PHASE8_SOURCES
    src/mc_cfr_deep.cpp
    src/preflop_solver.cpp
    src/exploit_engine.cpp
    src/dashboard_gen.cpp
    src/multi_agent_sim.cpp
)

add_library(phase8 STATIC ${PHASE8_SOURCES})
target_include_directories(phase8
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase1/include
        ${CMAKE_SOURCE_DIR}/phase2/include
        ${CMAKE_SOURCE_DIR}/phase3/include
        ${CMAKE_SOURCE_DIR}/phase4/include
        ${CMAKE_SOURCE_DIR}/phase5/include
        ${CMAKE_SOURCE_DIR}/phase6/include
        ${CMAKE_SOURCE_DIR}/phase7/include
)
target_link_libraries(phase8 PUBLIC phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)
