if (BUILD_TESTING AND KARMA_BUILD_TESTS)
  if (TARGET karma::headless)
    add_executable(karma_prefab_tests
      tests/prefab_tests.cpp
    )
    target_link_libraries(karma_prefab_tests PRIVATE karma::headless karma_features_visual)
    add_test(NAME karma_prefab_tests COMMAND karma_prefab_tests)

    add_executable(karma_scene_document_tests
      tests/scene_document_tests.cpp
    )
    target_link_libraries(karma_scene_document_tests PRIVATE karma::headless)
    add_test(NAME karma_scene_document_tests COMMAND karma_scene_document_tests)

    add_executable(karma_scene_runtime_tests
      tests/scene_runtime_tests.cpp
    )
    target_link_libraries(karma_scene_runtime_tests PRIVATE karma::headless)
    add_test(NAME karma_scene_runtime_tests COMMAND karma_scene_runtime_tests)

    add_executable(karma_scene_bake_tests
      tests/scene_bake_tests.cpp
    )
    target_link_libraries(karma_scene_bake_tests PRIVATE karma::headless)
    add_test(NAME karma_scene_bake_tests COMMAND karma_scene_bake_tests)

    if (TARGET karma_scene_bake)
      add_test(NAME karma_scene_bake_cli_check
        COMMAND ${CMAKE_COMMAND}
          -DKARMA_SCENE_BAKE=$<TARGET_FILE:karma_scene_bake>
          -DKARMA_SCENE_BAKE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tests
          -P ${PROJECT_SOURCE_DIR}/tests/scene_bake_cli_check.cmake
      )
    endif()

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

    if (TARGET karma_particle_effect_tools_lib)
      add_executable(karma_particle_generation_tests
        tests/particle_generation_tests.cpp
      )
      target_link_libraries(karma_particle_generation_tests
        PRIVATE
          karma::headless
          karma_particle_effect_tools_lib
      )
      add_test(NAME karma_particle_generation_tests COMMAND karma_particle_generation_tests)
    endif()

    add_executable(karma_physics_tests
      tests/physics_tests.cpp
    )
    target_link_libraries(karma_physics_tests PRIVATE karma::headless)
    add_test(NAME karma_physics_tests COMMAND karma_physics_tests)

    add_executable(karma_terrain_tests
      tests/terrain_tests.cpp
    )
    target_link_libraries(karma_terrain_tests PRIVATE karma::headless)
    add_test(NAME karma_terrain_tests COMMAND karma_terrain_tests)
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
      tests/navigation/navigation_system_tests.cpp
      tests/navigation/navmesh_build_tests.cpp
      tests/navigation/navmesh_crowd_tests.cpp
      tests/navigation/navmesh_query_tests.cpp
      tests/navigation/navmesh_test_utils.cpp
      tests/navigation/navmesh_tile_cache_tests.cpp
    )
    target_link_libraries(karma_navmesh_tests
      PRIVATE
        karma::headless
        karma_content
        karma_rendering_headless
        karma_simulation_navigation
    )
    add_test(NAME karma_navmesh_tests COMMAND karma_navmesh_tests)
  endif()
endif()
