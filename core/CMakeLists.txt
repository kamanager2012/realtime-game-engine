set(CORE_SOURCES
    src/logging.cpp
    src/serialization.cpp
    src/card.cpp
    src/evaluator.cpp
    src/hand_id.cpp
    src/suit_iso.cpp
    src/range.cpp
)

add_library(core STATIC ${CORE_SOURCES})
target_include_directories(core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
# fmt is needed transitively by downstream headers (logging.h -> fmt/format.h),
# so keep it PUBLIC; pheval is a pure implementation detail -> PRIVATE.
target_link_libraries(core PUBLIC Threads::Threads fmt::fmt
                           PRIVATE pheval)
target_include_directories(core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/PokerHandEvaluator/include>
)

# Precompiled header: the vendored header-only libs are parsed by every TU.
# This materially cuts full-build compile time.
target_precompile_headers(core PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/poker_engine/base/pch.h>"
)
