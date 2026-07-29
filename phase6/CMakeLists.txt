set(PHASE6_SOURCES
    src/icfr_solver.cpp
    src/range_tracker.cpp
    src/hand_database.cpp
    src/api_server.cpp
)

add_library(phase6 STATIC ${PHASE6_SOURCES})
target_include_directories(phase6
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
)
target_link_libraries(phase6 PUBLIC phase5 phase4 phase2 phase1 core equity)

# Optional SQLite3 support
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_compile_definitions(phase6 PUBLIC HAS_SQLITE3)
    target_link_libraries(phase6 PUBLIC SQLite::SQLite3)
    message(STATUS "SQLite3 found - hand database enabled")
else()
    message(STATUS "SQLite3 not found - hand database disabled")
endif()

# Optional cpp-httplib
find_package(Threads REQUIRED)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/httplib/httplib.h")
    target_compile_definitions(phase6 PRIVATE HAS_CPPHTTPLIB)
    target_include_directories(phase6 PRIVATE "${CMAKE_SOURCE_DIR}/third_party/httplib")
    message(STATUS "cpp-httplib found - API server enabled")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/httplib/httplib.h")
    target_compile_definitions(phase6 PRIVATE HAS_CPPHTTPLIB)
    target_include_directories(phase6 PRIVATE "${CMAKE_SOURCE_DIR}/external/httplib")
    message(STATUS "cpp-httplib found - API server enabled")
endif()
