set(PHASE12_SOURCES
    src/action.cpp
    src/player_state.cpp
    src/pot_manager.cpp
    src/action_validator.cpp
    src/showdown_evaluator.cpp
    src/dealer.cpp
    src/game_state.cpp
    src/table.cpp
    src/protocol.cpp
)

add_library(phase12 STATIC ${PHASE12_SOURCES})
target_include_directories(phase12
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include
)
find_package(OpenSSL REQUIRED)
target_link_libraries(phase12 PUBLIC core pheval OpenSSL::Crypto)
