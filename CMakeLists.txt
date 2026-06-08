cmake_minimum_required(VERSION 3.20)

project(karma VERSION 0.1.0 LANGUAGES CXX C)

if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING
    "Minimum CMake policy version used when configuring fetched dependencies")
endif()

if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(KARMA_IS_TOP_LEVEL ON)
else()
  set(KARMA_IS_TOP_LEVEL OFF)
endif()

option(BUILD_TESTING "Build tests" ${KARMA_IS_TOP_LEVEL})
if (BUILD_TESTING)
  include(CTest)
endif()

include(cmake/KarmaOptions.cmake)
include(cmake/KarmaDependencies.cmake)
include(cmake/KarmaEngine.cmake)
if (KARMA_BUILD_EXAMPLES)
  include(cmake/KarmaExamples.cmake)
endif()
include(cmake/KarmaTests.cmake)
include(cmake/KarmaDocs.cmake)
include(cmake/KarmaInstall.cmake)
