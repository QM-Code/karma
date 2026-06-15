cmake_minimum_required(VERSION 3.20)

project(karma_install_consumer_smoke LANGUAGES CXX)

option(KARMA_CONFIG_FETCH_DEPS "Allow karmaConfig.cmake to fetch missing package dependencies" OFF)
find_package(karma CONFIG REQUIRED)

add_executable(karma_install_consumer_smoke ../main.cpp)
target_link_libraries(karma_install_consumer_smoke PRIVATE karma::headless)

add_executable(karma_install_server_consumer_smoke ../server_main.cpp)
target_link_libraries(karma_install_server_consumer_smoke PRIVATE karma::server)

enable_testing()
add_test(NAME karma_install_consumer_smoke COMMAND karma_install_consumer_smoke)
add_test(NAME karma_install_server_consumer_smoke COMMAND karma_install_server_consumer_smoke)
