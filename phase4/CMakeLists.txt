set(PHASE4_SOURCES
    src/hh_parser.cpp
    src/multi_street_solver.cpp
    src/session_analyzer.cpp
)

add_library(phase4 STATIC ${PHASE4_SOURCES})
target_include_directories(phase4
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase1/include
        ${CMAKE_SOURCE_DIR}/phase2/include
        ${CMAKE_SOURCE_DIR}/phase3/include
)
target_link_libraries(phase4 PUBLIC core equity phase1 phase2 phase3)
