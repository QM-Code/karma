cmake_minimum_required(VERSION 3.20)

project(karma LANGUAGES CXX C)

set(BUILD_TESTING ON CACHE BOOL "Disable CTest targets for Karma deps" FORCE)
include(CTest)

include(cmake/KarmaOptions.cmake)
include(cmake/KarmaDependencies.cmake)
include(cmake/KarmaEngine.cmake)
include(cmake/KarmaExamples.cmake)
include(cmake/KarmaTests.cmake)
include(cmake/KarmaDocs.cmake)
