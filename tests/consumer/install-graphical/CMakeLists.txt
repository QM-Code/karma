cmake_minimum_required(VERSION 3.20)

project(karma_install_graphical_consumer_smoke LANGUAGES CXX C)

option(KARMA_CONFIG_FETCH_DEPS "Allow karmaConfig.cmake to fetch missing package dependencies" OFF)
find_package(karma CONFIG REQUIRED)

add_executable(karma_install_graphical_consumer_smoke ../graphical_main.cpp)
target_link_libraries(karma_install_graphical_consumer_smoke PRIVATE karma::graphical)

add_executable(karma_install_alias_consumer_smoke ../graphical_main.cpp)
target_link_libraries(karma_install_alias_consumer_smoke PRIVATE karma::karma)

enable_testing()
add_test(NAME karma_install_graphical_consumer_smoke COMMAND karma_install_graphical_consumer_smoke)
add_test(NAME karma_install_alias_consumer_smoke COMMAND karma_install_alias_consumer_smoke)
