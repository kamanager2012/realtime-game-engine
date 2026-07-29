set(PHASE2_SOURCES
    src/range_builder.cpp
    src/range_visualizer.cpp
    src/solver_node.cpp
)

add_library(phase2 STATIC ${PHASE2_SOURCES})
target_include_directories(phase2
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/phase1/include
)
target_link_libraries(phase2 PUBLIC core equity phase1)
