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
