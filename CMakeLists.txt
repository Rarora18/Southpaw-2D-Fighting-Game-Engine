cmake_minimum_required(VERSION 3.16)
project(Southpaw VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SOUTHPAW_BUILD_GAME "Build the raylib front end" ON)

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

# ---------------------------------------------------------------- tests & bench
add_executable(southpaw_tests tests/test_main.cpp tests/alloc_counter.cpp)
target_link_libraries(southpaw_tests PRIVATE southpaw_core)
target_compile_definitions(southpaw_tests PRIVATE SOUTHPAW_DATA_DIR="${SOUTHPAW_DATA_DIR}")
target_compile_options(southpaw_tests PRIVATE ${SOUTHPAW_WARNINGS})

add_executable(southpaw_bench bench/bench_main.cpp tests/alloc_counter.cpp)
target_link_libraries(southpaw_bench PRIVATE southpaw_core)
target_compile_definitions(southpaw_bench PRIVATE SOUTHPAW_DATA_DIR="${SOUTHPAW_DATA_DIR}")
target_compile_options(southpaw_bench PRIVATE ${SOUTHPAW_WARNINGS})

enable_testing()
add_test(NAME southpaw_tests COMMAND southpaw_tests)

# ---------------------------------------------------------------- game
if(SOUTHPAW_BUILD_GAME)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND)
    message(STATUS "raylib not found, fetching 5.5")
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
      URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(raylib)
  endif()

  add_executable(southpaw
    src/game/main.cpp
    src/game/pose.cpp
    src/game/renderer.cpp
    src/game/sfx.cpp
  )
  target_link_libraries(southpaw PRIVATE southpaw_core raylib)
  target_compile_definitions(southpaw PRIVATE SOUTHPAW_DATA_DIR="${SOUTHPAW_DATA_DIR}")
  target_compile_options(southpaw PRIVATE ${SOUTHPAW_WARNINGS})
  if(APPLE)
    target_link_libraries(southpaw PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
  endif()
endif()
