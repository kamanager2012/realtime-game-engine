set(PHASE11_SOURCES
    src/solver_manager.cpp
    src/fast_preflop_solver.cpp
    src/decision_engine.cpp
    src/engine_registry.cpp
)

add_library(phase11 STATIC ${PHASE11_SOURCES})
target_include_directories(phase11
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/equity/include
        ${CMAKE_SOURCE_DIR}/phase1/include
        ${CMAKE_SOURCE_DIR}/phase2/include
        ${CMAKE_SOURCE_DIR}/phase3/include
        ${CMAKE_SOURCE_DIR}/phase4/include
        ${CMAKE_SOURCE_DIR}/phase5/include
        ${CMAKE_SOURCE_DIR}/phase6/include
        ${CMAKE_SOURCE_DIR}/phase7/include
        ${CMAKE_SOURCE_DIR}/phase8/include
        ${CMAKE_SOURCE_DIR}/phase9/include
        ${CMAKE_SOURCE_DIR}/phase10/include
)
target_link_libraries(phase11 PUBLIC phase10 phase9 phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)
