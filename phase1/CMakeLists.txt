set(PHASE1_SOURCES
    src/hand_history.cpp
    src/ev_replay.cpp
)

add_library(phase1 STATIC ${PHASE1_SOURCES})
target_include_directories(phase1
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(phase1 PUBLIC core equity)
