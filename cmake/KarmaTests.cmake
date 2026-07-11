function(karma_enable_test_assertions target)
  # The test sources use assert() for checks and exercised calls. Keep them
  # active even when the surrounding build type defines NDEBUG.
  if (MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

if (BUILD_TESTING AND KARMA_BUILD_TESTS)
  if (TARGET karma_content)
    add_executable(karma_ui_json_profile_tests
      tests/ui_json_profile_tests.cpp
    )
    target_include_directories(karma_ui_json_profile_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_json_profile_tests PRIVATE karma_content)
    add_test(NAME karma_ui_json_profile_tests COMMAND karma_ui_json_profile_tests)
  endif()

  if (TARGET karma_core)
    add_executable(karma_cursor_tests
      tests/cursor_tests.cpp
    )
    target_link_libraries(karma_cursor_tests PRIVATE karma_core)
    add_test(NAME karma_cursor_tests COMMAND karma_cursor_tests)
  endif()

  if (TARGET karma::headless)
    add_executable(karma_core_runtime_tests
      tests/core_runtime_tests.cpp
    )
    target_link_libraries(karma_core_runtime_tests PRIVATE karma::headless)
    add_test(NAME karma_core_runtime_tests COMMAND karma_core_runtime_tests)

    add_executable(karma_audio_tests
      tests/audio_tests.cpp
    )
    target_include_directories(karma_audio_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(karma_audio_tests PRIVATE karma::headless)
    add_test(NAME karma_audio_tests COMMAND karma_audio_tests)

    add_executable(karma_collider_geometry_tests
      tests/collider_geometry_tests.cpp
    )
    target_link_libraries(karma_collider_geometry_tests PRIVATE karma::headless)
    add_test(NAME karma_collider_geometry_tests COMMAND karma_collider_geometry_tests)

    add_executable(karma_asset_cache_tests
      tests/asset_cache_tests.cpp
    )
    target_link_libraries(karma_asset_cache_tests PRIVATE karma_content)
    add_test(NAME karma_asset_cache_tests COMMAND karma_asset_cache_tests)

    add_executable(karma_ui_asset_package_tests
      tests/ui_asset_package_tests.cpp
    )
    target_link_libraries(karma_ui_asset_package_tests PRIVATE karma_content)
    add_test(NAME karma_ui_asset_package_tests COMMAND karma_ui_asset_package_tests)

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

    add_executable(karma_foliage_tests
      tests/foliage_tests.cpp
    )
    target_link_libraries(karma_foliage_tests PRIVATE karma::headless)
    add_test(NAME karma_foliage_tests COMMAND karma_foliage_tests)

    add_executable(karma_scene_authoring_tests
      tests/scene_authoring_tests.cpp
    )
    target_link_libraries(karma_scene_authoring_tests PRIVATE karma::headless)
    add_test(NAME karma_scene_authoring_tests COMMAND karma_scene_authoring_tests)

    if (TARGET karma_scene_editor_model)
      add_executable(karma_scene_editor_model_tests
        tests/scene_editor_model_tests.cpp
      )
      target_link_libraries(karma_scene_editor_model_tests
        PRIVATE karma::headless karma_scene_editor_model)
      add_test(NAME karma_scene_editor_model_tests COMMAND karma_scene_editor_model_tests)

      add_executable(karma_scene_editor_gizmo_tests
        tests/scene_editor_gizmo_tests.cpp
      )
      target_link_libraries(karma_scene_editor_gizmo_tests
        PRIVATE karma_scene_editor_model karma::headless)
      add_test(NAME karma_scene_editor_gizmo_tests
        COMMAND karma_scene_editor_gizmo_tests)

      add_executable(karma_scene_editor_viewport_tests
        tests/scene_editor_viewport_tests.cpp
      )
      target_link_libraries(karma_scene_editor_viewport_tests
        PRIVATE karma_scene_editor_model karma::headless)
      add_test(NAME karma_scene_editor_viewport_tests
        COMMAND karma_scene_editor_viewport_tests)

      add_executable(karma_scene_editor_collider_tests
        tests/scene_editor_collider_tests.cpp
      )
      target_link_libraries(karma_scene_editor_collider_tests
        PRIVATE karma_scene_editor_model karma::headless)
      add_test(NAME karma_scene_editor_collider_tests
        COMMAND karma_scene_editor_collider_tests)

      add_executable(karma_scene_editor_placement_tests
        tests/scene_editor_placement_tests.cpp
      )
      target_link_libraries(karma_scene_editor_placement_tests
        PRIVATE karma_scene_editor_model karma::headless)
      add_test(NAME karma_scene_editor_placement_tests
        COMMAND karma_scene_editor_placement_tests)
    endif()
  endif()

  if (TARGET karma_media_graphical AND KARMA_AUDIO_BACKEND_SDL)
    add_executable(karma_audio_sdl_tests
      tests/audio_sdl_tests.cpp
    )
    target_include_directories(karma_audio_sdl_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(karma_audio_sdl_tests PRIVATE karma_media_graphical)
    add_test(NAME karma_audio_sdl_tests COMMAND karma_audio_sdl_tests)
    set_tests_properties(karma_audio_sdl_tests PROPERTIES
      ENVIRONMENT "SDL_AUDIODRIVER=dummy"
    )
  endif()

  if (TARGET karma_features_ui_native)
    add_executable(karma_ui_accessibility_builder_tests
      tests/ui_accessibility_builder_tests.cpp
    )
    target_include_directories(karma_ui_accessibility_builder_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_accessibility_builder_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_accessibility_builder_tests
      COMMAND karma_ui_accessibility_builder_tests)

    add_executable(karma_ui_development_path_tests
      tests/ui_development_path_tests.cpp
      src/features/ui/native/development_path.cpp
    )
    target_include_directories(karma_ui_development_path_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_compile_features(karma_ui_development_path_tests PRIVATE cxx_std_20)
    add_test(NAME karma_ui_development_path_tests
      COMMAND karma_ui_development_path_tests)

    add_executable(karma_ui_authoring_tests
      tests/ui_authoring_tests.cpp
    )
    target_include_directories(karma_ui_authoring_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_compile_definitions(karma_ui_authoring_tests PRIVATE
      KARMA_UI_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/schemas/ui"
      KARMA_UI_SHOWCASE_DIR="${PROJECT_SOURCE_DIR}/examples/assets/ui/showcase"
    )
    target_link_libraries(karma_ui_authoring_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_authoring_tests COMMAND karma_ui_authoring_tests)

    add_executable(karma_ui_binding_tests
      tests/ui_binding_tests.cpp
    )
    target_include_directories(karma_ui_binding_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_binding_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_binding_tests COMMAND karma_ui_binding_tests)

    add_executable(karma_ui_document_reconciler_tests
      tests/ui_document_reconciler_tests.cpp
    )
    target_include_directories(karma_ui_document_reconciler_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_document_reconciler_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_document_reconciler_tests
      COMMAND karma_ui_document_reconciler_tests)

    add_executable(karma_ui_document_runtime_tests
      tests/ui_document_runtime_tests.cpp
    )
    target_include_directories(karma_ui_document_runtime_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_document_runtime_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_document_runtime_tests
      COMMAND karma_ui_document_runtime_tests)

    add_executable(karma_ui_listener_registry_tests
      tests/ui_listener_registry_tests.cpp
    )
    target_include_directories(karma_ui_listener_registry_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_listener_registry_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_listener_registry_tests
      COMMAND karma_ui_listener_registry_tests)

    add_executable(karma_ui_file_watcher_tests
      tests/ui_file_watcher_tests.cpp
    )
    target_include_directories(karma_ui_file_watcher_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_file_watcher_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_file_watcher_tests COMMAND karma_ui_file_watcher_tests)

    add_executable(karma_ui_hot_reload_coordinator_tests
      tests/ui_hot_reload_coordinator_tests.cpp
    )
    target_include_directories(karma_ui_hot_reload_coordinator_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_hot_reload_coordinator_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_hot_reload_coordinator_tests
      COMMAND karma_ui_hot_reload_coordinator_tests)

    add_executable(karma_ui_focus_runtime_tests
      tests/ui_focus_runtime_tests.cpp
    )
    target_include_directories(karma_ui_focus_runtime_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_focus_runtime_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_focus_runtime_tests
      COMMAND karma_ui_focus_runtime_tests)

    add_executable(karma_ui_transient_runtime_tests
      tests/ui_transient_runtime_tests.cpp
    )
    target_include_directories(karma_ui_transient_runtime_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_transient_runtime_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_transient_runtime_tests
      COMMAND karma_ui_transient_runtime_tests)

    add_executable(karma_ui_tests
      tests/ui_tests.cpp
    )
    target_link_libraries(karma_ui_tests PRIVATE karma_features_ui_native)
    add_test(NAME karma_ui_tests COMMAND karma_ui_tests)

    add_executable(karma_ui_layout_tests
      tests/ui_layout_tests.cpp
    )
    target_include_directories(karma_ui_layout_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(karma_ui_layout_tests PRIVATE karma_features_ui_native)
    add_test(NAME karma_ui_layout_tests COMMAND karma_ui_layout_tests)

    add_executable(karma_ui_document_layout_runtime_tests
      tests/ui_document_layout_runtime_tests.cpp
    )
    target_include_directories(karma_ui_document_layout_runtime_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_document_layout_runtime_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_document_layout_runtime_tests
      COMMAND karma_ui_document_layout_runtime_tests)

    add_executable(karma_ui_motion_tests
      tests/ui_motion_tests.cpp
    )
    target_include_directories(karma_ui_motion_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(karma_ui_motion_tests PRIVATE karma_features_ui_native)
    add_test(NAME karma_ui_motion_tests COMMAND karma_ui_motion_tests)

    add_executable(karma_ui_style_runtime_tests
      tests/ui_style_runtime_tests.cpp
    )
    target_include_directories(karma_ui_style_runtime_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_style_runtime_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_style_runtime_tests
      COMMAND karma_ui_style_runtime_tests)

    add_executable(karma_ui_paint_tests
      tests/ui_paint_tests.cpp
    )
    target_include_directories(karma_ui_paint_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(karma_ui_paint_tests PRIVATE karma_features_ui_native)
    add_test(NAME karma_ui_paint_tests COMMAND karma_ui_paint_tests)

    add_executable(karma_ui_widget_paint_tests
      tests/ui_widget_paint_tests.cpp
      src/features/ui/native/paint_engine.cpp
      src/features/ui/native/widget_paint.cpp
    )
    target_include_directories(karma_ui_widget_paint_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_widget_paint_tests PRIVATE karma_core)
    add_test(NAME karma_ui_widget_paint_tests COMMAND karma_ui_widget_paint_tests)

    add_executable(karma_ui_widget_runtime_tests
      tests/ui_widget_runtime_tests.cpp
    )
    target_include_directories(karma_ui_widget_runtime_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_widget_runtime_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_widget_runtime_tests
      COMMAND karma_ui_widget_runtime_tests)

    add_executable(karma_ui_presentation_tests
      tests/ui_presentation_tests.cpp
    )
    target_include_directories(karma_ui_presentation_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(karma_ui_presentation_tests PRIVATE
      KARMA_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/examples/assets")
    target_link_libraries(karma_ui_presentation_tests PRIVATE karma_features_ui_native)
    add_test(NAME karma_ui_presentation_tests COMMAND karma_ui_presentation_tests)

    add_executable(karma_ui_presentation_builder_tests
      tests/ui_presentation_builder_tests.cpp
    )
    target_include_directories(karma_ui_presentation_builder_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(karma_ui_presentation_builder_tests
      PRIVATE karma_features_ui_native
    )
    add_test(NAME karma_ui_presentation_builder_tests
      COMMAND karma_ui_presentation_builder_tests)

    add_executable(karma_ui_screenshot_golden_tests
      tests/ui_screenshot_golden_tests.cpp
    )
    target_include_directories(karma_ui_screenshot_golden_tests
      PRIVATE ${PROJECT_SOURCE_DIR}/src
    )
    target_compile_definitions(karma_ui_screenshot_golden_tests PRIVATE
      KARMA_UI_GOLDEN_ASSET_DIR="${PROJECT_SOURCE_DIR}/examples/assets/ui/native_menu"
    )
    target_link_libraries(karma_ui_screenshot_golden_tests PRIVATE karma_features_ui_native)
    add_test(NAME karma_ui_screenshot_golden_tests COMMAND karma_ui_screenshot_golden_tests)
  endif()

  if (TARGET karma_platform_window_graphical AND KARMA_WINDOW_BACKEND_SDL)
    add_executable(karma_window_sdl_tests
      tests/window_sdl_tests.cpp
    )
    target_link_libraries(karma_window_sdl_tests PRIVATE karma_platform_window_graphical)
    add_test(NAME karma_window_sdl_tests COMMAND karma_window_sdl_tests)
    set_tests_properties(karma_window_sdl_tests PROPERTIES SKIP_RETURN_CODE 77)
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

  set(_karma_assertion_test_targets
    karma_core_runtime_tests
    karma_audio_tests
    karma_collider_geometry_tests
    karma_asset_cache_tests
    karma_prefab_tests
    karma_scene_document_tests
    karma_scene_runtime_tests
    karma_scene_bake_tests
    karma_animation_tests
    karma_rendering_tests
    karma_particle_generation_tests
    karma_physics_tests
    karma_terrain_tests
    karma_foliage_tests
    karma_scene_authoring_tests
    karma_scene_editor_model_tests
    karma_scene_editor_gizmo_tests
    karma_scene_editor_viewport_tests
    karma_scene_editor_collider_tests
    karma_scene_editor_placement_tests
    karma_audio_sdl_tests
    karma_window_sdl_tests
    karma_network_tests
    karma_navmesh_tests
  )
  foreach(_karma_test_target IN LISTS _karma_assertion_test_targets)
    if (TARGET ${_karma_test_target})
      karma_enable_test_assertions(${_karma_test_target})
    endif()
  endforeach()
endif()
