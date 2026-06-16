if (KARMA_BUILD_GRAPHICAL_PROFILE)
  add_executable(karma_example
    examples/main.cpp
  )

  target_link_libraries(karma_example PRIVATE karma::graphical)

  if (KARMA_DILIGENT_REPACK_XXHASH
      AND KARMA_XXHASH_ARCHIVE
      AND CMAKE_AR
      AND CMAKE_RANLIB
      AND CMAKE_C_COMPILER
      AND UNIX
      AND NOT MSVC)
    set(KARMA_XXHASH_STAMP "${CMAKE_BINARY_DIR}/karma_xxhash.stamp")
    add_custom_command(
      OUTPUT ${KARMA_XXHASH_STAMP}
      COMMAND ${CMAKE_C_COMPILER} -c ${KARMA_XXHASH_SOURCE} -o ${KARMA_XXHASH_OBJECT}
      COMMAND ${CMAKE_AR} qc ${KARMA_XXHASH_ARCHIVE} ${KARMA_XXHASH_OBJECT}
      COMMAND ${CMAKE_RANLIB} ${KARMA_XXHASH_ARCHIVE}
      COMMAND ${CMAKE_COMMAND} -E touch ${KARMA_XXHASH_STAMP}
      COMMENT "Rebuilding xxhash static archive"
      VERBATIM)
    add_custom_target(karma_fix_xxhash DEPENDS ${KARMA_XXHASH_STAMP})
    add_dependencies(karma_example karma_fix_xxhash)
  endif()

  add_executable(karma_collision_events_example
    examples/collision_events_example.cpp
  )
  target_link_libraries(karma_collision_events_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_collision_events_example karma_fix_xxhash)
  endif()

  add_executable(karma_light_stress_example
    examples/light_stress_example.cpp
  )
  target_link_libraries(karma_light_stress_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_light_stress_example karma_fix_xxhash)
  endif()

  add_executable(karma_material_override_example
    examples/material_override_example.cpp
  )
  target_link_libraries(karma_material_override_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_material_override_example karma_fix_xxhash)
  endif()

  add_executable(karma_glb_scene_import_example
    examples/glb_scene_import_example.cpp
  )
  target_link_libraries(karma_glb_scene_import_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_glb_scene_import_example karma_fix_xxhash)
  endif()

  add_executable(karma_postwar_city_example
    examples/postwar_city_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_postwar_city_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_postwar_city_example karma_fix_xxhash)
  endif()

  add_executable(karma_diligent_gltf_viewer_example
    examples/diligent_gltf_viewer_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_diligent_gltf_viewer_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_diligent_gltf_viewer_example karma_fix_xxhash)
  endif()

  add_executable(karma_diligentfx_postprocess_example
    examples/diligentfx_postprocess_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_diligentfx_postprocess_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_diligentfx_postprocess_example karma_fix_xxhash)
  endif()

  add_executable(karma_diligentfx_bloom_example
    examples/diligentfx_bloom_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_diligentfx_bloom_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_diligentfx_bloom_example karma_fix_xxhash)
  endif()

  add_executable(karma_glb_animation_example
    examples/glb_animation_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_glb_animation_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_glb_animation_example karma_fix_xxhash)
  endif()

  add_executable(karma_particle_example
    examples/particle_example.cpp
  )
  target_link_libraries(karma_particle_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_particle_example karma_fix_xxhash)
  endif()

  add_executable(karma_energy_orb_example
    examples/energy_orb_example.cpp
  )
  target_link_libraries(karma_energy_orb_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_energy_orb_example karma_fix_xxhash)
  endif()

  add_executable(karma_particle_gallery_example
    examples/particle_gallery_example.cpp
  )
  target_link_libraries(karma_particle_gallery_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_particle_gallery_example karma_fix_xxhash)
  endif()

  add_executable(karma_laser_example
    examples/laser_example.cpp
  )
  target_link_libraries(karma_laser_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_laser_example karma_fix_xxhash)
  endif()

  add_executable(karma_laser_prefab_example
    examples/laser_prefab_example.cpp
  )
  target_link_libraries(karma_laser_prefab_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_laser_prefab_example karma_fix_xxhash)
  endif()

  add_executable(karma_wave_example
    examples/wave_example.cpp
  )
  target_link_libraries(karma_wave_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_wave_example karma_fix_xxhash)
  endif()

  add_executable(karma_volumetric_sphere_example
    examples/volumetric_sphere_example.cpp
  )
  target_link_libraries(karma_volumetric_sphere_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_volumetric_sphere_example karma_fix_xxhash)
  endif()

  add_executable(karma_volumetric_sphere_prefab_example
    examples/volumetric_sphere_prefab_example.cpp
  )
  target_link_libraries(karma_volumetric_sphere_prefab_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_volumetric_sphere_prefab_example karma_fix_xxhash)
  endif()

  add_executable(karma_terrain_example
    examples/terrain_example.cpp
  )
  target_link_libraries(karma_terrain_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_terrain_example karma_fix_xxhash)
  endif()

  add_executable(karma_prefab_gallery_example
    examples/prefab_gallery_example.cpp
  )
  target_link_libraries(karma_prefab_gallery_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_prefab_gallery_example karma_fix_xxhash)
  endif()

  add_executable(karma_prefab_particle_isolation_example
    examples/prefab_particle_isolation_example.cpp
  )
  target_link_libraries(karma_prefab_particle_isolation_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_prefab_particle_isolation_example karma_fix_xxhash)
  endif()

  add_executable(karma_explosion_stress_example
    examples/explosion_stress_example.cpp
  )
  target_link_libraries(karma_explosion_stress_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_explosion_stress_example karma_fix_xxhash)
  endif()

  if (KARMA_BUILD_IMGUI_DEMO)
    add_executable(karma_imgui_ui_demo
      examples/imgui_ui_demo.cpp
    )
    target_link_libraries(karma_imgui_ui_demo PRIVATE karma::graphical)
    if (TARGET karma_fix_xxhash)
      add_dependencies(karma_imgui_ui_demo karma_fix_xxhash)
    endif()
  endif()

  if (KARMA_BUILD_RMLUI_DEMO)
    add_executable(karma_rmlui_ui_demo
      examples/rmlui_ui_demo.cpp
    )
    target_link_libraries(karma_rmlui_ui_demo PRIVATE karma::graphical)
    if (TARGET karma_fix_xxhash)
      add_dependencies(karma_rmlui_ui_demo karma_fix_xxhash)
    endif()
  endif()
endif()

if (KARMA_NETWORK_BACKEND_ENET AND KARMA_BUILD_SERVER_PROFILE)
  add_executable(karma_network_server_demo
    examples/network_server_demo.cpp
    examples/network_demo_shared.cpp
  )

  target_link_libraries(karma_network_server_demo PRIVATE karma::server)
endif()

if (KARMA_NETWORK_BACKEND_ENET AND KARMA_BUILD_GRAPHICAL_PROFILE)
  add_executable(karma_network_client_demo
    examples/network_client_demo.cpp
    examples/network_demo_shared.cpp
  )

  target_link_libraries(karma_network_client_demo PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_network_client_demo karma_fix_xxhash)
  endif()
endif()

if (KARMA_ENABLE_NAVIGATION AND TARGET karma::headless)
  add_executable(karma_recast_navigation_examples
    examples/recast_navigation_examples.cpp
  )
  target_link_libraries(karma_recast_navigation_examples
    PRIVATE
      karma_content
      karma_simulation_navigation
      karma_rendering_headless
      karma::headless
  )
  if (BUILD_TESTING)
    add_test(NAME karma_recast_navigation_examples
      COMMAND karma_recast_navigation_examples all
    )
  endif()
endif()

if (KARMA_ENABLE_NAVIGATION AND KARMA_BUILD_GRAPHICAL_PROFILE)
  add_executable(karma_navmesh_example
    examples/navmesh_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_navmesh_example PRIVATE karma::graphical)

  function(karma_add_recast_sample target source)
    add_executable(${target}
      ${source}
      examples/recast_navigation_sample_app.cpp
      examples/scene_helpers.cpp
    )
    target_link_libraries(${target} PRIVATE karma::graphical)
    if (TARGET karma_fix_xxhash)
      add_dependencies(${target} karma_fix_xxhash)
    endif()
  endfunction()

  karma_add_recast_sample(karma_recast_solo_mesh_example
    examples/recast_solo_mesh_example.cpp)
  karma_add_recast_sample(karma_recast_tile_mesh_example
    examples/recast_tile_mesh_example.cpp)
  karma_add_recast_sample(karma_recast_temp_obstacles_example
    examples/recast_temp_obstacles_example.cpp)
  karma_add_recast_sample(karma_recast_debug_example
    examples/recast_debug_example.cpp)

  function(karma_add_navigation_example target source)
    add_executable(${target}
      ${source}
      examples/navigation/navigation_example_scene.cpp
      examples/navigation/navigation_examples.cpp
      examples/scene_helpers.cpp
    )
    target_link_libraries(${target} PRIVATE karma::graphical)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/navigation"
    )
    if (TARGET karma_fix_xxhash)
      add_dependencies(${target} karma_fix_xxhash)
    endif()
  endfunction()

  karma_add_navigation_example(point_click
    examples/navigation/point_click.cpp)
  karma_add_navigation_example(crowds
    examples/navigation/crowds.cpp)
  karma_add_navigation_example(tile_cache
    examples/navigation/tile_cache.cpp)
  karma_add_navigation_example(query_lab
    examples/navigation/query_lab.cpp)
  karma_add_navigation_example(offmesh_areas
    examples/navigation/offmesh_areas.cpp)
  karma_add_navigation_example(physics_bridge
    examples/navigation/physics_bridge.cpp)

  add_executable(karma_recast_navigation_graphical_example
    examples/recast_navigation_graphical_example.cpp
    examples/scene_helpers.cpp
  )
  target_link_libraries(karma_recast_navigation_graphical_example PRIVATE karma::graphical)
  if (TARGET karma_fix_xxhash)
    add_dependencies(karma_recast_navigation_graphical_example karma_fix_xxhash)
  endif()
endif()
