if (BUILD_TESTING)
  add_executable(karma_prefab_tests
    tests/prefab_tests.cpp
  )
  target_link_libraries(karma_prefab_tests PRIVATE karma)
  add_test(NAME karma_prefab_tests COMMAND karma_prefab_tests)

  add_executable(karma_animation_tests
    tests/animation_tests.cpp
  )
  target_link_libraries(karma_animation_tests PRIVATE karma)
  add_test(NAME karma_animation_tests COMMAND karma_animation_tests)
endif()

if (KARMA_ENABLE_NAVIGATION AND BUILD_TESTING)
  add_executable(karma_navmesh_tests
    tests/navmesh_tests.cpp
  )
  target_link_libraries(karma_navmesh_tests PRIVATE karma)
  add_test(NAME karma_navmesh_tests COMMAND karma_navmesh_tests)
endif()
