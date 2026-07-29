set(PHASE9_SOURCES
    src/variance_engine.cpp
    src/strategy_diff.cpp
    src/polarized_range.cpp
    src/pipeline_orchestrator.cpp
    src/advanced_batch_analyzer.cpp
)

add_library(phase9 STATIC ${PHASE9_SOURCES})
target_include_directories(phase9
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
        ${CMAKE_SOURCE_DIR}/phase8/include
)
target_link_libraries(phase9 PUBLIC phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)
