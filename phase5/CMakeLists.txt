set(PHASE5_SOURCES
    src/bulk_hh_parser.cpp
    src/equity_matrix.cpp
    src/icm_calc.cpp
    src/regression_analyzer.cpp
    src/hand_generator.cpp
)

add_library(phase5 STATIC ${PHASE5_SOURCES})
target_include_directories(phase5
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase1/include
        ${CMAKE_SOURCE_DIR}/phase2/include
        ${CMAKE_SOURCE_DIR}/phase3/include
        ${CMAKE_SOURCE_DIR}/phase4/include
)
target_link_libraries(phase5 PUBLIC core equity phase1 phase2 phase3 phase4)
