cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" KARMA_PROJECT_VERSION)
string(STRIP "${KARMA_PROJECT_VERSION}" KARMA_PROJECT_VERSION)

project(karma VERSION ${KARMA_PROJECT_VERSION} LANGUAGES CXX C)

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
include(cmake/KarmaTools.cmake)
if (KARMA_BUILD_EXAMPLES)
  include(cmake/KarmaExamples.cmake)
endif()
include(cmake/KarmaTests.cmake)
include(cmake/KarmaDocs.cmake)
include(cmake/KarmaInstall.cmake)
