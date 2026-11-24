cmake_minimum_required(VERSION 3.16)
project(Southpaw VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  set(SOUTHPAW_WARNINGS /W4)
else()
  set(SOUTHPAW_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
endif()

# ---------------------------------------------------------------- core (no raylib)
add_library(southpaw_core STATIC
  src/core/ai.cpp
  src/core/combat.cpp
  src/core/input.cpp
  src/core/json.cpp
  src/core/move_data.cpp
  src/core/replay.cpp
  src/core/sim.cpp
)
target_include_directories(southpaw_core PUBLIC src)
target_compile_options(southpaw_core PRIVATE ${SOUTHPAW_WARNINGS})

set(SOUTHPAW_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

# ---------------------------------------------------------------- tests
add_executable(southpaw_tests tests/test_main.cpp tests/alloc_counter.cpp)
target_link_libraries(southpaw_tests PRIVATE southpaw_core)
target_compile_definitions(southpaw_tests PRIVATE SOUTHPAW_DATA_DIR="${SOUTHPAW_DATA_DIR}")
target_compile_options(southpaw_tests PRIVATE ${SOUTHPAW_WARNINGS})

enable_testing()
add_test(NAME southpaw_tests COMMAND southpaw_tests)
