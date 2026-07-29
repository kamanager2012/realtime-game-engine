cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 99)

# Core (Hold'em / 5-7 card) evaluator — no large generated tables required.
set(PHEVAL_SOURCES
  src/dptables.c
  src/tables_bitwise.c
  src/evaluator5.c
  src/evaluator5.cc
  src/evaluator6.c
  src/evaluator6.cc
  src/evaluator7.c
  src/evaluator7.cc
  src/hashtable5.c
  src/hashtable6.c
  src/hashtable7.c
  src/hashtable.c
  src/hash.c
  src/rank.c
  src/7462.c
)

# Omaha (PLO4/5/6) needs large pre-generated lookup tables
# (tables_plo4.c, tables_plo5.c, tables_plo6.c). They are omitted from the
# distribution to keep the repo lean. To enable Omaha, drop the table files
# into src/ and reconfigure (see scripts/gen_omaha_tables.sh).
set(PHEVAL_PLO_SOURCES
  src/evaluator_plo4.c
  src/evaluator_plo4.cc
  src/tables_plo4.c
  src/evaluator_plo5.c
  src/evaluator_plo5.cc
  src/evaluator_plo6.c
  src/evaluator_plo6.cc
)

set(PHEVAL_HAVE_PLO OFF)
foreach(_tbl tables_plo4.c tables_plo5.c tables_plo6.c)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${_tbl}")
    set(PHEVAL_HAVE_PLO ON)
  endif()
endforeach()

if(PHEVAL_HAVE_PLO)
  list(APPEND PHEVAL_SOURCES ${PHEVAL_PLO_SOURCES})
endif()

add_library(pheval STATIC ${PHEVAL_SOURCES})
target_include_directories(pheval PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(PHEVAL_HAVE_PLO)
  target_compile_definitions(pheval PUBLIC PHEVAL_HAVE_PLO)
endif()

target_include_directories(pheval PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_options(pheval PRIVATE -O3 -w)
