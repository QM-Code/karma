cmake_minimum_required(VERSION 3.20)

project(karma_source_consumer_smoke LANGUAGES CXX)

if (NOT DEFINED KARMA_SOURCE_DIR)
  message(FATAL_ERROR "Set KARMA_SOURCE_DIR to the Karma source tree.")
endif()

set(KARMA_FETCH_DEPS ON CACHE BOOL "" FORCE)
set(KARMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(KARMA_HEADLESS ON CACHE BOOL "" FORCE)
set(KARMA_ENABLE_NAVIGATION OFF CACHE BOOL "" FORCE)
set(KARMA_NETWORK_BACKEND_ENET OFF CACHE BOOL "" FORCE)
set(KARMA_PHYSICS_BACKEND_JOLT OFF CACHE BOOL "" FORCE)
set(KARMA_PHYSICS_BACKEND_BULLET OFF CACHE BOOL "" FORCE)

add_subdirectory("${KARMA_SOURCE_DIR}" karma-build)

add_executable(karma_source_consumer_smoke ../main.cpp)
target_link_libraries(karma_source_consumer_smoke PRIVATE karma::headless)

enable_testing()
add_test(NAME karma_source_consumer_smoke COMMAND karma_source_consumer_smoke)
