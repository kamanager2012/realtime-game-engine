find_package(SQLite3 REQUIRED)

set(PHASE14_SOURCES
    src/database.cpp
    src/migration.cpp
    src/hand_repository.cpp
    src/player_repository.cpp
    src/stat_repository.cpp
    src/query_builder.cpp
    src/account_repository.cpp
    src/chip_ledger.cpp
    src/postgres_mirror.cpp
    src/database_manager.cpp
)

add_library(phase14 STATIC ${PHASE14_SOURCES})
target_include_directories(phase14
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
        ${CMAKE_SOURCE_DIR}/phase11/include
        ${CMAKE_SOURCE_DIR}/phase12/include
)
target_link_libraries(phase14 PUBLIC phase12 phase11 phase10 phase9 phase8
    phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity
    ${SQLite3_LIBRARIES}
)
target_include_directories(phase14 PRIVATE ${SQLite3_INCLUDE_DIRS})

find_package(PostgreSQL QUIET)
if(PostgreSQL_FOUND)
  target_link_libraries(phase14 PUBLIC PostgreSQL::PostgreSQL)
  target_compile_definitions(phase14 PUBLIC POKER_HAVE_LIBPQ=1)
  message(STATUS "PostgreSQL mirror enabled (libpq)")
else()
  message(STATUS "PostgreSQL mirror stub (libpq not found)")
endif()
