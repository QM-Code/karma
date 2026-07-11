if (KARMA_BUILD_TOOLS)
  add_library(karma_particle_effect_tools_lib STATIC
    tools/particles/particle_effect_tools.cpp
  )
  target_include_directories(karma_particle_effect_tools_lib
    PUBLIC
      ${PROJECT_SOURCE_DIR}/tools/particles
  )
  target_link_libraries(karma_particle_effect_tools_lib
    PUBLIC
      karma_content
  )
  target_compile_features(karma_particle_effect_tools_lib PUBLIC cxx_std_20)

  add_executable(karma_particle_effect_validate
    tools/particles/particle_effect_validate_main.cpp
  )
  target_link_libraries(karma_particle_effect_validate PRIVATE karma_particle_effect_tools_lib)

  add_executable(karma_particle_effect_format
    tools/particles/particle_effect_format_main.cpp
  )
  target_link_libraries(karma_particle_effect_format PRIVATE karma_particle_effect_tools_lib)

  add_executable(karma_particle_effect_generate
    tools/particles/particle_effect_generate_main.cpp
  )
  target_link_libraries(karma_particle_effect_generate PRIVATE karma_particle_effect_tools_lib)

  add_executable(karma_scene_bake
    tools/scenes/scene_bake_main.cpp
  )
  target_link_libraries(karma_scene_bake PRIVATE karma_content)

  add_library(karma_scene_editor_model STATIC
    tools/scenes/scene_editor_model.cpp
    tools/scenes/scene_editor_foliage_prefab.cpp
    tools/scenes/scene_editor_migration.cpp
    tools/scenes/scene_editor_pointer_input.cpp
    tools/scenes/scene_editor_viewport.cpp
    tools/scenes/scene_editor_gizmo.cpp
    tools/scenes/scene_editor_markers.cpp
    tools/scenes/scene_editor_colliders.cpp
    tools/scenes/scene_editor_placement.cpp
  )
  target_include_directories(karma_scene_editor_model
    PUBLIC
      ${PROJECT_SOURCE_DIR}/tools/scenes
  )
  target_link_libraries(karma_scene_editor_model PUBLIC karma_content)
  target_compile_features(karma_scene_editor_model PUBLIC cxx_std_20)

  if (KARMA_BUILD_GRAPHICAL_PROFILE AND KARMA_BUILD_SCENE_EDITOR)
    set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
    # NFDe 1.3 compares these unquoted tokens as variables. Define their
    # literal values so the platform/compiler source selection also works with
    # modern CMake policy behavior inherited from Karma.
    set(PLATFORM_WIN32 PLATFORM_WIN32)
    set(PLATFORM_LINUX PLATFORM_LINUX)
    set(PLATFORM_MACOS PLATFORM_MACOS)
    set(COMPILER_MSVC COMPILER_MSVC)
    set(COMPILER_CLANGCL COMPILER_CLANGCL)
    set(COMPILER_GNU COMPILER_GNU)
    FetchContent_Declare(
      nativefiledialog
      GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
      GIT_TAG v1.3.0
    )
    FetchContent_MakeAvailable(nativefiledialog)

    add_executable(karma_scene_editor
      tools/scenes/scene_editor_main.cpp
    )
    target_link_libraries(karma_scene_editor
      PRIVATE
        karma::graphical
        karma_features_ui_imgui
        karma_scene_editor_model
        nfd::nfd
    )
    target_compile_definitions(karma_scene_editor PRIVATE
      KARMA_SCENE_EDITOR_FONT_SOURCE_DIR="${PROJECT_SOURCE_DIR}/tools/scenes/assets/fonts"
    )
    target_compile_features(karma_scene_editor PRIVATE cxx_std_20)
    set_target_properties(karma_scene_editor PROPERTIES
      OUTPUT_NAME scene_editor
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/scenes"
    )
    add_custom_command(TARGET karma_scene_editor POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/tools/scenes/assets/fonts"
        "$<TARGET_FILE_DIR:karma_scene_editor>/assets/fonts"
      COMMENT "Copying Scene Editor fonts"
    )
    if (TARGET karma_fix_xxhash)
      add_dependencies(karma_scene_editor karma_fix_xxhash)
    endif()
  endif()

  if (BUILD_TESTING AND KARMA_BUILD_TESTS)
    file(GLOB_RECURSE KARMA_COMMITTED_PARTICLE_EFFECTS
      CONFIGURE_DEPENDS
      "${PROJECT_SOURCE_DIR}/examples/*.kpeffect"
    )
    if (KARMA_COMMITTED_PARTICLE_EFFECTS)
      add_test(NAME karma_particle_effect_validate_committed
        COMMAND karma_particle_effect_validate ${KARMA_COMMITTED_PARTICLE_EFFECTS}
      )
      add_test(NAME karma_particle_effect_format_check_committed
        COMMAND karma_particle_effect_format --check ${KARMA_COMMITTED_PARTICLE_EFFECTS}
      )
    endif()
  endif()
endif()
