
# Eigen3: check if already provided by parent (vendored), otherwise search
if(NOT TARGET Eigen3::Eigen)
    find_package(Eigen3 QUIET)
endif()
if(TARGET Eigen3::Eigen)
    set(Eigen3_FOUND TRUE)
endif()

set(PHASE18_SOURCES
    src/hyperparam.cpp
    src/hyperparam_tuner.cpp
    src/balancer.cpp
)

set(PHASE18_HEADERS
    include/poker_engine/cfr/hyperparam.h
    include/poker_engine/cfr/hyperparam_tuner.h
    include/poker_engine/tournament/balancer.h
)

if(Eigen3_FOUND)
    list(APPEND PHASE18_SOURCES
        src/bayesian_optimizer.cpp
        src/sobol_analysis.cpp
    )
    list(APPEND PHASE18_HEADERS
        include/poker_engine/cfr/bayesian_optimizer.h
        include/poker_engine/cfr/sobol_analysis.h
    )
else()
    message(WARNING "Eigen3 not found — phase18 bayesian_optimizer and sobol_analysis disabled. Install libeigen3-dev or set CMAKE_PREFIX_PATH.")
endif()

add_library(phase18 STATIC ${PHASE18_SOURCES} ${PHASE18_HEADERS})

target_include_directories(phase18
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/phase12/include
        ${CMAKE_SOURCE_DIR}/phase13/include
        ${CMAKE_SOURCE_DIR}/phase14/include
        ${CMAKE_SOURCE_DIR}/phase15/include
        ${CMAKE_SOURCE_DIR}/phase16/include
        ${CMAKE_SOURCE_DIR}/phase17/include
)

target_link_libraries(phase18
    PUBLIC
        poker_engine_core
        phase15
    PRIVATE
        Threads::Threads
)

if(Eigen3_FOUND)
    target_link_libraries(phase18 PRIVATE Eigen3::Eigen)
    target_compile_definitions(phase18 PRIVATE HAVE_EIGEN3=1)
endif()

option(POKER_ENGINE_ENABLE_TUNING "Enable hyperparameter tuning module" ON)
if(POKER_ENGINE_ENABLE_TUNING)
    target_compile_definitions(phase18 PRIVATE ENABLE_TUNING)
endif()
