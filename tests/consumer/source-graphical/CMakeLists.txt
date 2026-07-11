cmake_minimum_required(VERSION 3.20)

project(karma_source_graphical_consumer_smoke LANGUAGES CXX C)

if (NOT DEFINED KARMA_SOURCE_DIR)
  message(FATAL_ERROR "Set KARMA_SOURCE_DIR to the Karma source tree.")
endif()

set(KARMA_FETCH_DEPS ON CACHE BOOL "" FORCE)
set(KARMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_SERVER_PROFILE OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_HEADLESS_PROFILE OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_GRAPHICAL_PROFILE ON CACHE BOOL "" FORCE)
set(KARMA_ENABLE_AUDIO OFF CACHE BOOL "" FORCE)
set(KARMA_ENABLE_KTX2 OFF CACHE BOOL "" FORCE)
set(KARMA_ENABLE_NATIVE_UI ON CACHE BOOL "" FORCE)
set(KARMA_ENABLE_IMGUI OFF CACHE BOOL "" FORCE)
set(KARMA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_DEBUG_UI OFF CACHE BOOL "" FORCE)
set(KARMA_ENABLE_NAVIGATION OFF CACHE BOOL "" FORCE)
set(KARMA_NETWORK_BACKEND_ENET OFF CACHE BOOL "" FORCE)
set(KARMA_PHYSICS_BACKEND_JOLT OFF CACHE BOOL "" FORCE)
set(KARMA_PHYSICS_BACKEND_BULLET OFF CACHE BOOL "" FORCE)

add_subdirectory("${KARMA_SOURCE_DIR}" karma-build)

add_executable(karma_source_graphical_consumer_smoke ../graphical_main.cpp)
target_link_libraries(karma_source_graphical_consumer_smoke PRIVATE karma::graphical)
target_compile_definitions(karma_source_graphical_consumer_smoke PRIVATE
  KARMA_TEST_EXPECT_NATIVE_UI)

add_executable(karma_source_alias_consumer_smoke ../graphical_main.cpp)
target_link_libraries(karma_source_alias_consumer_smoke PRIVATE karma::karma)
target_compile_definitions(karma_source_alias_consumer_smoke PRIVATE
  KARMA_TEST_EXPECT_NATIVE_UI)

enable_testing()
if (WIN32)
  message(STATUS
    "Skipping source-vendored graphical consumer runtime smoke tests on Windows; "
    "configure/build still verifies package linkage.")
else()
  add_test(NAME karma_source_graphical_consumer_smoke COMMAND karma_source_graphical_consumer_smoke)
  add_test(NAME karma_source_alias_consumer_smoke COMMAND karma_source_alias_consumer_smoke)
endif()
