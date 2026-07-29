set(EQUITY_SOURCES src/equity_calculator.cpp)
add_library(equity STATIC ${EQUITY_SOURCES})
target_include_directories(equity
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(equity PUBLIC core)
