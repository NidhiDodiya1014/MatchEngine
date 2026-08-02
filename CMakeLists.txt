cmake_minimum_required(VERSION 3.16)
project(matchengine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(Threads REQUIRED)

add_library(matchengine_core
    src/order_book.cpp
)
target_include_directories(matchengine_core PUBLIC include)
target_link_libraries(matchengine_core PUBLIC Threads::Threads)

add_executable(matchengine_demo src/main.cpp)
target_link_libraries(matchengine_demo PRIVATE matchengine_core)

# --- Tests (GoogleTest via FetchContent, same pattern as TurboQuery) ---
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(matchengine_tests
    tests/test_order_book.cpp
    tests/test_spsc_queue.cpp
    tests/test_matching_engine.cpp
)
target_link_libraries(matchengine_tests PRIVATE matchengine_core gtest_main)
include(GoogleTest)
gtest_discover_tests(matchengine_tests)

# --- Benchmark ---
add_executable(bench_matching benchmarks/bench_matching.cpp)
target_link_libraries(bench_matching PRIVATE matchengine_core)
