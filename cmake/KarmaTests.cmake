if (BUILD_TESTING AND KARMA_BUILD_TESTS)
  if (TARGET karma::headless)
    add_executable(karma_prefab_tests
      tests/prefab_tests.cpp
    )
    target_link_libraries(karma_prefab_tests PRIVATE karma::headless karma_features_visual)
    add_test(NAME karma_prefab_tests COMMAND karma_prefab_tests)

    add_executable(karma_animation_tests
      tests/animation_tests.cpp
    )
    target_link_libraries(karma_animation_tests PRIVATE karma::headless)
    add_test(NAME karma_animation_tests COMMAND karma_animation_tests)

    add_executable(karma_rendering_tests
      tests/rendering_tests.cpp
    )
    target_link_libraries(karma_rendering_tests PRIVATE karma::headless)
    add_test(NAME karma_rendering_tests COMMAND karma_rendering_tests)
  endif()

  set(KARMA_NETWORK_TEST_PROFILE "")
  if (TARGET karma::server)
    set(KARMA_NETWORK_TEST_PROFILE karma::server)
  elseif (TARGET karma::headless)
    set(KARMA_NETWORK_TEST_PROFILE karma::headless)
  elseif (TARGET karma::graphical)
    set(KARMA_NETWORK_TEST_PROFILE karma::graphical)
  endif()

  if (KARMA_NETWORK_TEST_PROFILE)
    add_executable(karma_network_tests
      tests/network_tests.cpp
    )
    target_link_libraries(karma_network_tests PRIVATE ${KARMA_NETWORK_TEST_PROFILE})
    add_test(NAME karma_network_tests COMMAND karma_network_tests)
  endif()

  if (KARMA_ENABLE_NAVIGATION AND TARGET karma::headless)
    add_executable(karma_navmesh_tests
      tests/navmesh_tests.cpp
    )
    target_link_libraries(karma_navmesh_tests PRIVATE karma::headless)
    add_test(NAME karma_navmesh_tests COMMAND karma_navmesh_tests)
  endif()
endif()
