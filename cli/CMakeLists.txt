add_executable(replay_tool replay_tool.cpp)
target_link_libraries(replay_tool PRIVATE phase1 core equity)

add_executable(range_tool range_tool.cpp)
target_link_libraries(range_tool PRIVATE phase2 phase1 core equity)

add_executable(phase3_tool phase3_tool.cpp)
target_link_libraries(phase3_tool PRIVATE phase3 phase2 phase1 core equity)

add_executable(phase4_tool phase4_tool.cpp)
target_link_libraries(phase4_tool PRIVATE phase4 phase3 phase2 phase1 core equity)

add_executable(phase5_tool phase5_tool.cpp)
target_link_libraries(phase5_tool PRIVATE phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(phase6_tool phase6_tool.cpp)
target_link_libraries(phase6_tool PRIVATE phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(phase7_tool phase7_tool.cpp)
target_link_libraries(phase7_tool PRIVATE phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(phase8_tool phase8_tool.cpp)
target_link_libraries(phase8_tool PRIVATE phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(phase9_tool phase9_tool.cpp)
target_link_libraries(phase9_tool PRIVATE phase9 phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(phase10_tool phase10_tool.cpp)
target_link_libraries(phase10_tool PRIVATE phase10 phase9 phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(unified_tool unified_tool.cpp)
target_link_libraries(unified_tool PRIVATE phase11 phase10 phase9 phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(poker_server poker_server.cpp)
target_link_libraries(poker_server PRIVATE phase12 phase11 phase10 phase9 phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(poker_table poker_table.cpp)
target_link_libraries(poker_table PRIVATE phase12 phase11 phase10 phase9 phase8 phase7 phase6 phase5 phase4 phase3 phase2 phase1 core equity)

add_executable(poker_ws_server poker_ws_server.cpp)
target_include_directories(poker_ws_server PRIVATE ${CMAKE_SOURCE_DIR}/infra/net/include)
target_include_directories(poker_ws_server PRIVATE ${CMAKE_SOURCE_DIR}/phase19/include)
target_link_libraries(poker_ws_server PRIVATE poker_net phase13 phase14 phase15 phase16 phase17 phase21 phase12 core equity ${OPENSSL_CRYPTO_LIBRARY} SQLite::SQLite3)

add_executable(db_tool db_tool.cpp)
target_link_libraries(db_tool PRIVATE phase14)

add_executable(cfr_train_tool cfr_train_tool.cpp)
target_include_directories(cfr_train_tool PRIVATE ${CMAKE_SOURCE_DIR}/phase12/include)
target_link_libraries(cfr_train_tool PRIVATE phase15 phase12 core)

add_executable(agent_bench agent_bench.cpp)
target_include_directories(agent_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/phase13/include
    ${CMAKE_SOURCE_DIR}/phase15/include
    ${CMAKE_SOURCE_DIR}/phase12/include)
target_link_libraries(agent_bench PRIVATE arena phase13 phase15 phase12 core equity Threads::Threads)
