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
  src/core/combat.cpp
  src/core/input.cpp
  src/core/json.cpp
  src/core/move_data.cpp
)
target_include_directories(southpaw_core PUBLIC src)
target_compile_options(southpaw_core PRIVATE ${SOUTHPAW_WARNINGS})

set(SOUTHPAW_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
