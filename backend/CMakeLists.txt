cmake_minimum_required(VERSION 3.25)
project(smartcity VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build options ────────────────────────────────────────────────────────────
option(SMARTCITY_BUILD_TESTS  "Build unit tests"  ON)
option(SMARTCITY_BUILD_BENCH  "Build benchmarks"  ON)
option(SMARTCITY_ENABLE_ASAN  "Enable AddressSanitizer (Debug)"  OFF)

if(SMARTCITY_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ─── Threads ──────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ─── FetchContent dependencies (pinned versions) ──────────────────────────────
include(FetchContent)

# Standalone Asio fallback (if libasio-dev not installed on host)
find_path(SYSTEM_ASIO asio.hpp)
if(NOT SYSTEM_ASIO AND NOT DEFINED ASIO_INCLUDE_DIR)
  FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-28-0
    GIT_SHALLOW    TRUE
  )
  FetchContent_GetProperties(asio)
  if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
    set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "Asio include dir" FORCE)
  endif()
endif()

# Turn off Crow examples and tests
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Crow 1.2.0  https://github.com/CrowCpp/Crow
FetchContent_Declare(Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG        v1.2.0
  GIT_SHALLOW    TRUE
)

# nlohmann/json 3.11.3  https://github.com/nlohmann/json
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)

# Catch2 3.6.0  https://github.com/catchorg/Catch2
if(SMARTCITY_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
    GIT_SHALLOW    TRUE
  )
endif()

# Fetch all at once
if(SMARTCITY_BUILD_TESTS)
  FetchContent_MakeAvailable(Crow nlohmann_json Catch2)
else()
  FetchContent_MakeAvailable(Crow nlohmann_json)
endif()

# ─── Main server ──────────────────────────────────────────────────────────────
add_subdirectory(src)

# ─── Tests ────────────────────────────────────────────────────────────────────
if(SMARTCITY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ─── Benchmarks ───────────────────────────────────────────────────────────────
if(SMARTCITY_BUILD_BENCH)
  add_subdirectory(bench)
endif()
