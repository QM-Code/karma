cmake_minimum_required(VERSION 3.20)

project(karma_install_graphical_consumer_smoke LANGUAGES CXX C)

option(KARMA_CONFIG_FETCH_DEPS "Allow karmaConfig.cmake to fetch missing package dependencies" OFF)
find_package(karma CONFIG REQUIRED)

if (NOT DEFINED KARMA_UI_SCHEMA_DIR OR
    NOT EXISTS "${KARMA_UI_SCHEMA_DIR}/kui.v2.schema.json" OR
    NOT EXISTS "${KARMA_UI_SCHEMA_DIR}/kstyle.v2.schema.json")
  message(FATAL_ERROR "Installed Karma package is missing native UI schemas")
endif()

add_executable(karma_install_graphical_consumer_smoke ../graphical_main.cpp)
target_link_libraries(karma_install_graphical_consumer_smoke PRIVATE karma::graphical)
target_compile_definitions(karma_install_graphical_consumer_smoke PRIVATE
  KARMA_TEST_EXPECT_NATIVE_UI)

add_executable(karma_install_alias_consumer_smoke ../graphical_main.cpp)
target_link_libraries(karma_install_alias_consumer_smoke PRIVATE karma::karma)
target_compile_definitions(karma_install_alias_consumer_smoke PRIVATE
  KARMA_TEST_EXPECT_NATIVE_UI)

enable_testing()
if (WIN32)
  message(STATUS
    "Skipping installed-package graphical consumer runtime smoke tests on Windows; "
    "configure/build still verifies package linkage.")
else()
  add_test(NAME karma_install_graphical_consumer_smoke COMMAND karma_install_graphical_consumer_smoke)
  add_test(NAME karma_install_alias_consumer_smoke COMMAND karma_install_alias_consumer_smoke)
endif()
