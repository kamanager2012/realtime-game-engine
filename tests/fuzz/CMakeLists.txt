# Fuzz testing targets — requires clang with libFuzzer.
# Build: CC=clang CXX=clang++ cmake -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer,address" ..
# Run:   ./tests/fuzz/poker_fuzz -max_total_time=60

cmake_minimum_required(VERSION 3.16)

add_executable(card_parse_fuzz fuzz_targets.cpp
    ${CMAKE_SOURCE_DIR}/core/src/card.cpp
    ${CMAKE_SOURCE_DIR}/core/src/evaluator.cpp
)
target_include_directories(card_parse_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)
target_link_libraries(card_parse_fuzz PRIVATE -fsanitize=fuzzer,address)

add_executable(json_parse_fuzz fuzz_targets.cpp
    ${CMAKE_SOURCE_DIR}/phase13/src/auth_service.cpp
)
target_include_directories(json_parse_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/phase13/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann/include
)
target_link_libraries(json_parse_fuzz PRIVATE
    OpenSSL::Crypto nlohmann_json::nlohmann_json -fsanitize=fuzzer,address
)
