set(KARMA_EXAMPLES_COMMON_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/examples/common")

function(karma_configure_example target output_subdir output_name)
  target_include_directories(${target} PRIVATE
    "${KARMA_EXAMPLES_COMMON_INCLUDE_DIR}"
  )
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${output_name}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/${output_subdir}"
  )
  if (TARGET karma_fix_xxhash)
    add_dependencies(${target} karma_fix_xxhash)
  endif()
endfunction()

function(karma_add_graphical_example target output_subdir output_name)
  add_executable(${target}
    ${ARGN}
  )
  target_link_libraries(${target} PRIVATE karma::graphical)
  karma_configure_example(${target} "${output_subdir}" "${output_name}")
endfunction()

function(karma_add_physics_example target output_name)
  add_executable(${target}
    ${ARGN}
    examples/physics/physics_example_common.cpp
  )
  if (UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE
      "-Wl,--start-group"
      ${KARMA_GRAPHICAL_PROFILE_LIBS}
      "-Wl,--end-group")
  else()
    target_link_libraries(${target} PRIVATE karma::graphical)
  endif()
  karma_configure_example(${target} "physics" "${output_name}")
endfunction()

if (KARMA_BUILD_GRAPHICAL_PROFILE)
  if (KARMA_DILIGENT_REPACK_XXHASH
      AND KARMA_XXHASH_ARCHIVE
      AND KARMA_XXHASH_OBJECT
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
  endif()

  karma_add_graphical_example(gameplay_tank gameplay tank
    examples/gameplay/tank.cpp)
  karma_add_physics_example(physics_collision_events collision_events
    examples/physics/collision_events.cpp)

  karma_add_graphical_example(rendering_light_stress rendering light_stress
    examples/rendering/light_stress.cpp)
  karma_add_graphical_example(rendering_material_assignment rendering material_assignment
    examples/rendering/material_assignment.cpp)
  karma_add_graphical_example(rendering_grass_card rendering grass_card
    examples/rendering/grass_card.cpp)
  karma_add_graphical_example(rendering_grass_field rendering grass_field
    examples/rendering/grass_field.cpp)
  karma_add_graphical_example(rendering_postwar_city rendering postwar_city
    examples/rendering/postwar_city.cpp
    examples/common/scene_helpers.cpp)
  karma_add_graphical_example(rendering_gltf_viewer rendering gltf_viewer
    examples/rendering/gltf_viewer.cpp
    examples/common/scene_helpers.cpp)
  karma_add_graphical_example(rendering_postprocess rendering postprocess
    examples/rendering/postprocess.cpp
    examples/common/scene_helpers.cpp)
  karma_add_graphical_example(rendering_bloom rendering bloom
    examples/rendering/bloom.cpp
    examples/common/scene_helpers.cpp)

  karma_add_graphical_example(scene_gltf_import scene gltf_import
    examples/scene/gltf_import.cpp
    examples/common/scene_helpers.cpp)
  karma_add_graphical_example(animation_gltf animation gltf
    examples/animation/gltf.cpp
    examples/common/scene_helpers.cpp)

  karma_add_graphical_example(particles_billboard particles billboard
    examples/particles/billboard.cpp)
  karma_add_graphical_example(particles_gallery particles gallery
    examples/particles/gallery.cpp)
  karma_add_graphical_example(particles_explosion_stress particles explosion_stress
    examples/particles/explosion_stress.cpp)
  karma_add_graphical_example(particles_generated_preview particles generated_preview
    examples/particles/generated_preview.cpp)
  karma_add_graphical_example(particles_generated_scale_preview particles generated_scale_preview
    examples/particles/generated_preview.cpp)
  target_compile_definitions(particles_generated_scale_preview PRIVATE
    KARMA_PARTICLE_SCALE_REFERENCE_PREVIEW=1)
  if (TARGET karma_particle_effect_tools_lib)
    target_link_libraries(particles_generated_preview PRIVATE karma_particle_effect_tools_lib)
    target_link_libraries(particles_generated_scale_preview PRIVATE karma_particle_effect_tools_lib)
    target_compile_definitions(particles_generated_preview PRIVATE
      KARMA_PARTICLE_PREVIEW_GENERATION=1)
    target_compile_definitions(particles_generated_scale_preview PRIVATE
      KARMA_PARTICLE_PREVIEW_GENERATION=1)
  endif()

  karma_add_graphical_example(effects_energy_orb effects energy_orb
    examples/effects/energy_orb.cpp)
  karma_add_graphical_example(effects_laser effects laser
    examples/effects/laser.cpp)
  karma_add_graphical_example(effects_wave effects wave
    examples/effects/wave.cpp)
  karma_add_graphical_example(effects_volumetric_sphere effects volumetric_sphere
    examples/effects/volumetric_sphere.cpp)

  karma_add_graphical_example(rendering_terrain rendering terrain
    examples/rendering/terrain.cpp)

  karma_add_graphical_example(prefabs_laser prefabs laser
    examples/prefabs/laser.cpp)
  karma_add_graphical_example(prefabs_volumetric_sphere prefabs volumetric_sphere
    examples/prefabs/volumetric_sphere.cpp)
  karma_add_graphical_example(prefabs_gallery prefabs gallery
    examples/prefabs/gallery.cpp)
  karma_add_graphical_example(prefabs_particle_isolation prefabs particle_isolation
    examples/prefabs/particle_isolation.cpp)

  if (KARMA_BUILD_IMGUI_DEMO)
    karma_add_graphical_example(ui_imgui ui imgui
      examples/ui/imgui.cpp)
  endif()

  if (KARMA_BUILD_RMLUI_DEMO)
    karma_add_graphical_example(ui_rmlui ui rmlui
      examples/ui/rmlui.cpp)
  endif()

  karma_add_physics_example(physics_shape_gallery shape_gallery
    examples/physics/shape_gallery.cpp)
  karma_add_physics_example(physics_constraint_lab constraint_lab
    examples/physics/constraint_lab.cpp)
  karma_add_physics_example(physics_query_lab query_lab
    examples/physics/query_lab.cpp)
  karma_add_physics_example(physics_body_controls body_controls
    examples/physics/body_controls.cpp)
  karma_add_physics_example(physics_car car
    examples/physics/car.cpp)
endif()

if (KARMA_NETWORK_BACKEND_ENET AND KARMA_BUILD_SERVER_PROFILE)
  add_executable(network_server
    examples/network/server.cpp
    examples/network/shared.cpp
  )
  target_link_libraries(network_server PRIVATE karma::server)
  karma_configure_example(network_server "network" "server")
endif()

if (KARMA_NETWORK_BACKEND_ENET AND KARMA_BUILD_GRAPHICAL_PROFILE)
  karma_add_graphical_example(network_client network client
    examples/network/client.cpp
    examples/network/shared.cpp)
  if (KARMA_BUILD_IMGUI_DEMO)
    karma_add_graphical_example(network_discovery_directory network discovery_directory
      examples/network/discovery_directory.cpp)
  endif()
endif()

if (KARMA_ENABLE_NAVIGATION AND TARGET karma::headless)
  add_executable(navigation_samples_headless
    examples/navigation/samples/headless.cpp
  )
  target_link_libraries(navigation_samples_headless
    PRIVATE
      karma_content
      karma_simulation_navigation
      karma_rendering_headless
      karma::headless
  )
  karma_configure_example(navigation_samples_headless "navigation/samples" "headless")
  if (BUILD_TESTING)
    if (WIN32)
      message(STATUS
        "Skipping navigation_samples_headless CTest on Windows CI; "
        "the Recast sample runner can hang under CTest on Windows.")
    else()
      add_test(NAME navigation_samples_headless
        COMMAND navigation_samples_headless all
      )
      set_tests_properties(navigation_samples_headless PROPERTIES TIMEOUT 120)
    endif()
  endif()
endif()

if (KARMA_ENABLE_NAVIGATION AND KARMA_BUILD_GRAPHICAL_PROFILE)
  karma_add_graphical_example(navigation_navmesh navigation navmesh
    examples/navigation/navmesh.cpp
    examples/common/scene_helpers.cpp)

  function(karma_add_navigation_sample target output_name source)
    karma_add_graphical_example(${target} "navigation/samples" "${output_name}"
      ${source}
      examples/navigation/samples/sample_app.cpp
      examples/common/scene_helpers.cpp)
  endfunction()

  karma_add_navigation_sample(navigation_solo_mesh solo_mesh
    examples/navigation/samples/solo_mesh.cpp)
  karma_add_navigation_sample(navigation_tile_mesh tile_mesh
    examples/navigation/samples/tile_mesh.cpp)
  karma_add_navigation_sample(navigation_temp_obstacles temp_obstacles
    examples/navigation/samples/temp_obstacles.cpp)
  karma_add_navigation_sample(navigation_debug debug
    examples/navigation/samples/debug.cpp)

  function(karma_add_navigation_example target output_name source)
    karma_add_graphical_example(${target} "navigation" "${output_name}"
      ${source}
      examples/navigation/navigation_example_scene.cpp
      examples/navigation/navigation_examples.cpp
      examples/common/scene_helpers.cpp)
  endfunction()

  karma_add_navigation_example(navigation_point_click point_click
    examples/navigation/point_click.cpp)
  karma_add_navigation_example(navigation_crowds crowds
    examples/navigation/crowds.cpp)
  karma_add_navigation_example(navigation_tile_cache tile_cache
    examples/navigation/tile_cache.cpp)
  karma_add_navigation_example(navigation_query_lab query_lab
    examples/navigation/query_lab.cpp)
  karma_add_navigation_example(navigation_offmesh_areas offmesh_areas
    examples/navigation/offmesh_areas.cpp)
  karma_add_navigation_example(navigation_physics_bridge physics_bridge
    examples/navigation/physics_bridge.cpp)

  karma_add_graphical_example(navigation_samples_gallery "navigation/samples" gallery
    examples/navigation/samples/gallery.cpp
    examples/common/scene_helpers.cpp)
endif()
