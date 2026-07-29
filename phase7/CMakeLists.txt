set(PHASE7_SOURCES
    src/cfr_plus_solver.cpp
    src/tournament_icm.cpp
    src/opponent_modeler.cpp
    src/mtt_simulator.cpp
)

add_library(phase7 STATIC ${PHASE7_SOURCES})
target_include_directories(phase7
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
)
target_link_libraries(phase7 PUBLIC phase6 phase5 phase4 phase3 phase2 phase1 core equity)

find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(poker_engine_python ${CMAKE_SOURCE_DIR}/python/bindings.cpp)
    target_link_libraries(poker_engine_python PRIVATE phase7 phase6 phase5 phase4 core equity)
    set_target_properties(poker_engine_python PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/python
        OUTPUT_NAME "poker_engine"
    )
    message(STATUS "pybind11 found - Python bindings will be built")
else()
    message(STATUS "pybind11 not found - Python bindings disabled")
endif()

find_package(Threads REQUIRED)
