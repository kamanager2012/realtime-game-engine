# Optional examples. Enable with -DBUILD_EXAMPLES=ON.
#
# The LLM agent demo makes real HTTPS calls to an OpenAI-compatible endpoint
# when OPENAI_API_KEY is set, so it needs OpenSSL + the vendored httplib/json.
# It is off by default to keep the core build free of that runtime dependency.

find_package(OpenSSL REQUIRED)

add_executable(llm_agent_demo
    llm-agent/main.cpp
    llm-agent/llm_agent.cpp)

target_include_directories(llm_agent_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/llm-agent
    ${CMAKE_SOURCE_DIR}/phase13/include
    ${CMAKE_SOURCE_DIR}/phase12/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/third_party)

target_link_libraries(llm_agent_demo PRIVATE
    arena
    phase13
    phase12
    core
    equity
    Threads::Threads
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto)

target_compile_definitions(llm_agent_demo PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
