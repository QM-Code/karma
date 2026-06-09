cmake_minimum_required(VERSION 3.20)

project(karma_install_consumer_smoke LANGUAGES CXX)

set(KARMA_CONFIG_FETCH_DEPS OFF CACHE BOOL "" FORCE)
find_package(karma CONFIG REQUIRED)

add_executable(karma_install_consumer_smoke ../main.cpp)
target_link_libraries(karma_install_consumer_smoke PRIVATE karma::headless)

enable_testing()
add_test(NAME karma_install_consumer_smoke COMMAND karma_install_consumer_smoke)
