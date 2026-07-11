set(KARMA_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")

configure_file(
  ${PROJECT_SOURCE_DIR}/cmake/version.h.in
  ${KARMA_GENERATED_INCLUDE_DIR}/karma/version.h
  @ONLY
)

function(karma_configure_target target)
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${KARMA_GENERATED_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
  )
  target_compile_features(${target} PUBLIC cxx_std_20)
  foreach (karma_include_dir IN LISTS KARMA_EXTRA_INCLUDE_DIRS)
    target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${karma_include_dir}>)
  endforeach()
endfunction()

function(karma_configure_interface target)
  target_include_directories(${target}
    INTERFACE
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${KARMA_GENERATED_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )
  target_compile_features(${target} INTERFACE cxx_std_20)
  foreach (karma_include_dir IN LISTS KARMA_EXTRA_INCLUDE_DIRS)
    target_include_directories(${target} INTERFACE $<BUILD_INTERFACE:${karma_include_dir}>)
  endforeach()
endfunction()

function(karma_link_build_and_install target scope build_var install_var)
  foreach (karma_link_lib IN LISTS ${build_var})
    target_link_libraries(${target} ${scope} $<BUILD_INTERFACE:${karma_link_lib}>)
  endforeach()
  foreach (karma_link_lib IN LISTS ${install_var})
    target_link_libraries(${target} ${scope} $<INSTALL_INTERFACE:$<1:${karma_link_lib}>>)
  endforeach()
endfunction()

function(karma_add_static target)
  add_library(${target} STATIC ${ARGN})
  karma_configure_target(${target})
  karma_link_build_and_install(${target} PUBLIC KARMA_EXTRA_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
endfunction()

set(KARMA_INSTALL_TARGETS)

add_library(karma_core INTERFACE)
karma_configure_interface(karma_core)
karma_link_build_and_install(karma_core INTERFACE KARMA_EXTRA_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
list(APPEND KARMA_INSTALL_TARGETS karma_core)

karma_add_static(karma_world
  src/world/components/animator.cpp
  src/world/components/transform.cpp
  src/world/ecs/collider_queries.cpp
  src/world/geometry/primitive_meshes.cpp
  src/world/scene/transform_hierarchy.cpp
)
target_link_libraries(karma_world PUBLIC karma_core)
list(APPEND KARMA_INSTALL_TARGETS karma_world)

set(KARMA_RENDERER_CORE_SOURCES
  src/rendering/renderer/backend_factory.cpp
  src/rendering/renderer/camera_picking.cpp
  src/rendering/renderer/device.cpp
  src/rendering/renderer/frame_graph.cpp
  src/rendering/renderer/render_system.cpp
  src/rendering/renderer/render_system/debug_draw.cpp
  src/rendering/renderer/render_system/extractors.cpp
)

if (KARMA_BUILD_HEADLESS_PROFILE)
  karma_add_static(karma_rendering_headless
    ${KARMA_RENDERER_CORE_SOURCES}
  )
  target_link_libraries(karma_rendering_headless PUBLIC karma_core karma_world)
  list(APPEND KARMA_INSTALL_TARGETS karma_rendering_headless)
endif()

if (KARMA_BUILD_GRAPHICAL_PROFILE)
  set(KARMA_RENDERING_GRAPHICAL_SOURCES
    ${KARMA_RENDERER_CORE_SOURCES}
  )

  if (KARMA_RENDER_BACKEND_DILIGENT)
    list(APPEND KARMA_RENDERING_GRAPHICAL_SOURCES
      src/rendering/renderer/backends/diligent/backend_common.cpp
      src/rendering/renderer/backends/diligent/backend_init.cpp
      src/rendering/renderer/backends/diligent/backend_render.cpp
      src/rendering/renderer/backends/diligent/backend_textures.cpp
      src/rendering/renderer/backends/diligent/backend_ui.cpp
      src/rendering/renderer/backends/diligent/access.cpp
      src/rendering/renderer/backends/diligent/passes/beam.cpp
      src/rendering/renderer/backends/diligent/passes/camera_override.cpp
      src/rendering/renderer/backends/diligent/passes/environment.cpp
      src/rendering/renderer/backends/diligent/passes/frame.cpp
      src/rendering/renderer/backends/diligent/passes/frame_graph_shader.cpp
      src/rendering/renderer/backends/diligent/passes/forward.cpp
      src/rendering/renderer/backends/diligent/passes/line.cpp
      src/rendering/renderer/backends/diligent/passes/particle_draw.cpp
      src/rendering/renderer/backends/diligent/passes/particles.cpp
      src/rendering/renderer/backends/diligent/passes/post_process/chain.cpp
      src/rendering/renderer/backends/diligent/passes/post_process/common.cpp
      src/rendering/renderer/backends/diligent/passes/post_process/pipelines.cpp
      src/rendering/renderer/backends/diligent/passes/post_process/resources.cpp
      src/rendering/renderer/backends/diligent/passes/post_process/shader_source.cpp
      src/rendering/renderer/backends/diligent/passes/render_state.cpp
      src/rendering/renderer/backends/diligent/passes/shadows.cpp
      src/rendering/renderer/backends/diligent/passes/terrain.cpp
      src/rendering/renderer/backends/diligent/resources/deformations.cpp
      src/rendering/renderer/backends/diligent/resources/materials.cpp
      src/rendering/renderer/backends/diligent/resources/meshes.cpp
      src/rendering/renderer/backends/diligent/resources/render_targets.cpp
      src/rendering/renderer/backends/diligent/resources/textures.cpp
    )
  endif()

  karma_add_static(karma_rendering_graphical
    ${KARMA_RENDERING_GRAPHICAL_SOURCES}
  )
  target_link_libraries(karma_rendering_graphical PUBLIC karma_core karma_world)
  karma_link_build_and_install(karma_rendering_graphical PUBLIC KARMA_RENDER_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
  if (KARMA_RENDER_BACKEND_DILIGENT)
    target_compile_definitions(karma_rendering_graphical PUBLIC KARMA_RENDER_BACKEND_DILIGENT)
    target_compile_definitions(karma_rendering_graphical PRIVATE
      KARMA_DILIGENT_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/rendering/renderer/backends/diligent/shaders/post_process"
    )
    if (KARMA_WINDOW_BACKEND_SDL)
      target_compile_definitions(karma_rendering_graphical PUBLIC KARMA_WINDOW_BACKEND_SDL)
    endif()
    if (WIN32)
      target_compile_definitions(karma_rendering_graphical PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if (diligentcore_SOURCE_DIR)
      target_include_directories(karma_rendering_graphical PUBLIC $<BUILD_INTERFACE:${diligentcore_SOURCE_DIR}>)
      target_include_directories(karma_rendering_graphical PUBLIC $<BUILD_INTERFACE:${diligentcore_SOURCE_DIR}/Graphics/GraphicsTools/interface>)
    endif()
  endif()
  list(APPEND KARMA_INSTALL_TARGETS karma_rendering_graphical)
endif()

karma_add_static(karma_simulation_animation
  src/simulation/animation/animation_clip.cpp
  src/simulation/animation/animation_system.cpp
  src/simulation/animation/deformation_system.cpp
  src/simulation/animation/pose.cpp
  src/simulation/animation/retarget.cpp
)
target_link_libraries(karma_simulation_animation PUBLIC karma_core karma_world)
list(APPEND KARMA_INSTALL_TARGETS karma_simulation_animation)

karma_add_static(karma_simulation_collision
  src/simulation/collision/collision_event_system.cpp
)
target_link_libraries(karma_simulation_collision PUBLIC karma_core karma_world)
list(APPEND KARMA_INSTALL_TARGETS karma_simulation_collision)

karma_add_static(karma_simulation_physics
  src/simulation/physics/backend_factory.cpp
  src/simulation/physics/constraint.cpp
  src/simulation/physics/rigid_body.cpp
  src/simulation/physics/soft_body.cpp
  src/simulation/physics/vehicle.cpp
  src/simulation/physics/character_controller.cpp
  src/simulation/physics/physics_world.cpp
  src/simulation/physics/physics_system.cpp
  src/simulation/physics/physics_system_bodies.cpp
  src/simulation/physics/physics_system_character.cpp
  src/simulation/physics/physics_system_cleanup.cpp
  src/simulation/physics/physics_system_constraints.cpp
  src/simulation/physics/physics_system_conversions.cpp
  src/simulation/physics/physics_system_events.cpp
  src/simulation/physics/physics_system_soft_bodies.cpp
  src/simulation/physics/physics_system_vehicles.cpp
)
target_link_libraries(karma_simulation_physics PUBLIC karma_core karma_world)
karma_link_build_and_install(karma_simulation_physics PUBLIC KARMA_PHYSICS_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
if (KARMA_PHYSICS_BACKEND_JOLT)
  target_compile_definitions(karma_simulation_physics PUBLIC KARMA_PHYSICS_BACKEND_JOLT)
  target_sources(karma_simulation_physics PRIVATE
    src/simulation/physics/backends/jolt/constraint_jolt.cpp
    src/simulation/physics/backends/jolt/physics_world_jolt.cpp
    src/simulation/physics/backends/jolt/query_jolt.cpp
    src/simulation/physics/backends/jolt/rigid_body_jolt.cpp
    src/simulation/physics/backends/jolt/character_controller_jolt.cpp
    src/simulation/physics/backends/jolt/shape_factory.cpp
    src/simulation/physics/backends/jolt/soft_body_jolt.cpp
    src/simulation/physics/backends/jolt/vehicle_jolt.cpp
  )
endif()
if (KARMA_PHYSICS_BACKEND_BULLET)
  target_compile_definitions(karma_simulation_physics PUBLIC KARMA_PHYSICS_BACKEND_BULLET)
  target_sources(karma_simulation_physics PRIVATE
    src/simulation/physics/backends/bullet/physics_world_bullet.cpp
    src/simulation/physics/backends/bullet/rigid_body_bullet.cpp
    src/simulation/physics/backends/bullet/character_controller_bullet.cpp
  )
endif()
list(APPEND KARMA_INSTALL_TARGETS karma_simulation_physics)

if (KARMA_ENABLE_NAVIGATION)
  karma_add_static(karma_simulation_navigation
    src/simulation/navigation/nav_crowd.cpp
    src/simulation/navigation/nav_crowd_debug.cpp
    src/simulation/navigation/nav_cache.cpp
    src/simulation/navigation/nav_geometry.cpp
    src/simulation/navigation/nav_mesh.cpp
    src/simulation/navigation/nav_mesh_build.cpp
    src/simulation/navigation/nav_mesh_debug.cpp
    src/simulation/navigation/nav_mesh_render_debug.cpp
    src/simulation/navigation/nav_mesh_snapshot.cpp
    src/simulation/navigation/nav_mesh_state.cpp
    src/simulation/navigation/nav_query.cpp
    src/simulation/navigation/nav_query_debug.cpp
    src/simulation/navigation/nav_query_path.cpp
    src/simulation/navigation/nav_query_spatial.cpp
    src/simulation/navigation/nav_query_sliced.cpp
    src/simulation/navigation/nav_tile_cache.cpp
    src/simulation/navigation/nav_tile_cache_build.cpp
    src/simulation/navigation/nav_tile_cache_common.cpp
    src/simulation/navigation/nav_tile_cache_debug.cpp
    src/simulation/navigation/nav_tile_cache_runtime.cpp
    src/simulation/navigation/nav_tile_cache_snapshot.cpp
    src/simulation/navigation/navigation_diagnostics.cpp
    src/simulation/navigation/navigation_system.cpp
    src/simulation/navigation/navigation_system_crowd.cpp
    src/simulation/navigation/navigation_system_debug.cpp
    src/simulation/navigation/navigation_system_helpers.cpp
    src/simulation/navigation/navigation_system_navmesh.cpp
    src/simulation/navigation/navigation_system_tile_cache.cpp
    src/simulation/navigation/navigation_system_worker.cpp
    src/simulation/navigation/third_party/fastlz/fastlz.c
  )
  target_link_libraries(karma_simulation_navigation PUBLIC karma_core karma_world)
  target_include_directories(karma_simulation_navigation PRIVATE src/simulation/navigation/third_party/fastlz)
  karma_link_build_and_install(karma_simulation_navigation PUBLIC KARMA_NAVIGATION_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
  target_compile_definitions(karma_simulation_navigation PUBLIC KARMA_ENABLE_NAVIGATION DT_VIRTUAL_QUERYFILTER)
  list(APPEND KARMA_INSTALL_TARGETS karma_simulation_navigation)
endif()

if (KARMA_BUILD_HEADLESS_PROFILE)
  karma_add_static(karma_media_headless
    src/media/audio/audio.cpp
    src/media/audio/audio_system.cpp
    src/media/audio/backend_factory.cpp
  )
  target_link_libraries(karma_media_headless PUBLIC karma_core karma_world)
  list(APPEND KARMA_INSTALL_TARGETS karma_media_headless)
endif()

if (KARMA_BUILD_GRAPHICAL_PROFILE)
  set(KARMA_MEDIA_GRAPHICAL_SOURCES
    src/media/audio/audio.cpp
    src/media/audio/audio_system.cpp
    src/media/audio/backend_factory.cpp
  )
  if (KARMA_AUDIO_BACKEND_MINIAUDIO)
    list(APPEND KARMA_MEDIA_GRAPHICAL_SOURCES
      src/media/audio/backends/miniaudio/backend.cpp
      src/media/audio/backends/miniaudio/clip.cpp
    )
  endif()
  if (KARMA_AUDIO_BACKEND_SDL)
    list(APPEND KARMA_MEDIA_GRAPHICAL_SOURCES
      src/media/audio/backends/sdl/backend.cpp
      src/media/audio/backends/sdl/clip.cpp
    )
  endif()

  karma_add_static(karma_media_graphical
    ${KARMA_MEDIA_GRAPHICAL_SOURCES}
  )
  target_link_libraries(karma_media_graphical PUBLIC karma_core karma_world)
  karma_link_build_and_install(karma_media_graphical PUBLIC KARMA_AUDIO_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
  if (KARMA_AUDIO_BACKEND_MINIAUDIO)
    target_compile_definitions(karma_media_graphical PUBLIC KARMA_AUDIO_BACKEND_MINIAUDIO)
  endif()
  if (KARMA_AUDIO_BACKEND_SDL)
    target_compile_definitions(karma_media_graphical PUBLIC KARMA_AUDIO_BACKEND_SDL)
  endif()
  list(APPEND KARMA_INSTALL_TARGETS karma_media_graphical)
endif()

karma_add_static(karma_content
  src/content/assets/asset_cache.cpp
  src/content/assets/asset_cache_json.cpp
  src/content/assets/asset_cache_mesh.cpp
  src/content/assets/asset_cache_texture.cpp
  src/content/assets/asset_cache_ui.cpp
  src/content/assets/bake_artifacts.cpp
  src/content/assets/asset_package.cpp
  src/content/assets/asset_registry.cpp
  src/content/assets/asset_source_import.cpp
  src/content/assets/asset_texture.cpp
  src/content/assets/asset_ui_source_import.cpp
  src/content/assets/ui_json_profile.cpp
  src/content/assets/ui_json_validation.cpp
  src/content/importers/gltf_document.cpp
  src/content/importers/mesh_import.cpp
  src/content/importers/gltf_scene_animation_import.cpp
  src/content/importers/gltf_scene_mesh_import.cpp
  src/content/importers/gltf_scene_skinning.cpp
  src/content/importers/gltf_scene_import.cpp
  src/content/image/stb_image.cpp
  src/content/materials/material_loader.cpp
  src/content/prefabs/component_serializer_registry.cpp
  src/content/prefabs/component_serializer_rendering.cpp
  src/content/prefabs/prefab_render_migration.cpp
  src/content/prefabs/prefab_runtime.cpp
  src/content/scenes/scene_bake.cpp
  src/content/scenes/scene_bake_artifacts.cpp
  src/content/scenes/scene_light_bake.cpp
  src/content/scenes/scene_document.cpp
  src/content/scenes/scene_document_parser.cpp
  src/content/scenes/scene_runtime.cpp
  src/content/scenes/scene_runtime_assets.cpp
  src/content/scenes/scene_runtime_prefabs.cpp
  src/content/scenes/terrain_canvas.cpp
  src/features/visual/foliage/foliage_file.cpp
  src/features/visual/particles/effect_library.cpp
)
target_link_libraries(karma_content
  PUBLIC
    karma_core
    karma_world
    karma_simulation_animation
)
if (KARMA_MESHOPTIMIZER_TARGET)
  target_link_libraries(karma_content PRIVATE ${KARMA_MESHOPTIMIZER_TARGET})
endif()
if (KARMA_ENABLE_KTX2)
  target_compile_definitions(karma_content PUBLIC KARMA_ENABLE_KTX2)
  target_compile_definitions(karma_content PRIVATE KARMA_KTX_SOFTWARE_TAG="${KARMA_KTX_SOFTWARE_TAG}")
endif()
if (KARMA_ENABLE_NAVIGATION)
  target_sources(karma_content PRIVATE
    src/content/navigation/nav_geometry.cpp
    src/content/navigation/nav_tile_cache.cpp
  )
  target_link_libraries(karma_content PUBLIC karma_simulation_navigation)
endif()
list(APPEND KARMA_INSTALL_TARGETS karma_content)

karma_add_static(karma_platform_network
  src/platform/network/discovery.cpp
  src/platform/network/protocol.cpp
  src/platform/network/session.cpp
  src/platform/network/transport_factory.cpp
)
target_link_libraries(karma_platform_network PUBLIC karma_core)
karma_link_build_and_install(karma_platform_network PUBLIC KARMA_NETWORK_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
if (WIN32)
  target_compile_definitions(karma_platform_network PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(karma_platform_network PRIVATE ws2_32)
endif()
if (KARMA_NETWORK_BACKEND_ENET)
  target_compile_definitions(karma_platform_network PUBLIC KARMA_NETWORK_BACKEND_ENET)
  target_sources(karma_platform_network PRIVATE
    src/platform/network/enet_transport.cpp
  )
endif()
list(APPEND KARMA_INSTALL_TARGETS karma_platform_network)

if (KARMA_BUILD_HEADLESS_PROFILE)
  karma_add_static(karma_platform_window_headless
    src/platform/window/window_factory.cpp
  )
  target_link_libraries(karma_platform_window_headless PUBLIC karma_core)
  target_compile_definitions(karma_platform_window_headless PUBLIC KARMA_HEADLESS)
  list(APPEND KARMA_INSTALL_TARGETS karma_platform_window_headless)
endif()

if (KARMA_BUILD_GRAPHICAL_PROFILE)
  karma_add_static(karma_platform_window_graphical
    src/platform/window/window_factory.cpp
  )
  target_link_libraries(karma_platform_window_graphical PUBLIC karma_core)
  karma_link_build_and_install(karma_platform_window_graphical PUBLIC KARMA_PLATFORM_LINK_LIBS KARMA_INSTALL_LINK_LIBS)
  if (KARMA_RENDER_BACKEND_DILIGENT)
    target_compile_definitions(karma_platform_window_graphical PUBLIC KARMA_RENDER_BACKEND_DILIGENT)
  endif()
  if (KARMA_WINDOW_BACKEND_SDL)
    target_compile_definitions(karma_platform_window_graphical PUBLIC KARMA_WINDOW_BACKEND_SDL)
    target_sources(karma_platform_window_graphical PRIVATE
      src/platform/window/backends/window_sdl.cpp
    )
  elseif (KARMA_WINDOW_BACKEND_GLFW)
    target_sources(karma_platform_window_graphical PRIVATE
      src/platform/window/backends/window_glfw.cpp
    )
  endif()
  list(APPEND KARMA_INSTALL_TARGETS karma_platform_window_graphical)
endif()

karma_add_static(karma_features_network
  src/features/network/component_replication.cpp
)
target_link_libraries(karma_features_network PUBLIC karma_core karma_world karma_platform_network)
list(APPEND KARMA_INSTALL_TARGETS karma_features_network)

if (KARMA_BUILD_HEADLESS_PROFILE OR KARMA_BUILD_GRAPHICAL_PROFILE)
  karma_add_static(karma_features_visual
    src/features/visual/foliage/foliage_layer.cpp
    src/features/visual/foliage/foliage_render_prototype.cpp
    src/features/visual/foliage/foliage_runtime_module.cpp
    src/features/visual/lights/light_pulse_system.cpp
    src/features/visual/particles/particle_system.cpp
    src/features/visual/terrain/terrain_runtime_module.cpp
    src/features/visual/terrain/terrain_system.cpp
    src/features/visual/volumes/volume_runtime_module.cpp
    src/features/visual/volumes/volume_system.cpp
  )
  target_link_libraries(karma_features_visual PUBLIC karma_core karma_world karma_content)
  list(APPEND KARMA_INSTALL_TARGETS karma_features_visual)
endif()

set(KARMA_RUNTIME_COMMON_SOURCES
  src/runtime/app/engine_app.cpp
  src/runtime/app/ui_context.cpp
  src/runtime/input/input_system.cpp
)

if (KARMA_BUILD_HEADLESS_PROFILE)
  karma_add_static(karma_runtime_headless
    ${KARMA_RUNTIME_COMMON_SOURCES}
  )
  target_link_libraries(karma_runtime_headless
    PUBLIC
      karma_core
      karma_world
      karma_rendering_headless
      karma_media_headless
      karma_simulation_animation
      karma_simulation_collision
      karma_simulation_physics
      karma_content
      karma_features_network
      karma_features_visual
      karma_platform_network
      karma_platform_window_headless
  )
  target_compile_definitions(karma_runtime_headless PUBLIC KARMA_HEADLESS)
  if (KARMA_ENABLE_NAVIGATION)
    target_link_libraries(karma_runtime_headless PUBLIC karma_simulation_navigation)
    target_compile_definitions(karma_runtime_headless PUBLIC KARMA_ENABLE_NAVIGATION)
  endif()
  list(APPEND KARMA_INSTALL_TARGETS karma_runtime_headless)
endif()

if (KARMA_BUILD_GRAPHICAL_PROFILE)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(KARMA_YOGA_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:${KARMA_YOGA_TARGET}>")
    set(KARMA_FREETYPE_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:${KARMA_FREETYPE_TARGET}>")
    set(KARMA_HARFBUZZ_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:${KARMA_HARFBUZZ_TARGET}>")
    set(KARMA_ICU_UC_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:ICU::uc>")
    set(KARMA_ICU_I18N_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:ICU::i18n>")
    set(KARMA_LUNASVG_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:${KARMA_LUNASVG_TARGET}>")
  else()
    set(KARMA_YOGA_BUILD_LINK "$<BUILD_INTERFACE:${KARMA_YOGA_TARGET}>")
    set(KARMA_FREETYPE_BUILD_LINK "$<BUILD_INTERFACE:${KARMA_FREETYPE_TARGET}>")
    set(KARMA_HARFBUZZ_BUILD_LINK "$<BUILD_INTERFACE:${KARMA_HARFBUZZ_TARGET}>")
    set(KARMA_ICU_UC_BUILD_LINK "$<BUILD_INTERFACE:ICU::uc>")
    set(KARMA_ICU_I18N_BUILD_LINK "$<BUILD_INTERFACE:ICU::i18n>")
    set(KARMA_LUNASVG_BUILD_LINK "$<BUILD_INTERFACE:${KARMA_LUNASVG_TARGET}>")
  endif()
  if (KARMA_ENABLE_NATIVE_UI)
    karma_add_static(karma_features_ui_native
      src/features/ui/native/accessibility_builder.cpp
      src/features/ui/native/value.cpp
      src/features/ui/native/asset_reference.cpp
      src/features/ui/native/authoring.cpp
      src/features/ui/native/binding_engine.cpp
      src/features/ui/native/canvas_layout.cpp
      src/features/ui/native/computed_style_values.cpp
      src/features/ui/native/controller.cpp
      src/features/ui/native/diagnostics.cpp
      src/features/ui/native/development_path.cpp
      src/features/ui/native/document_loader.cpp
      src/features/ui/native/document_layout_runtime.cpp
      src/features/ui/native/document_reconciler.cpp
      src/features/ui/native/document_runtime.cpp
      src/features/ui/native/focus_runtime.cpp
      src/features/ui/native/hot_reload_coordinator.cpp
      src/features/ui/native/hot_reload_runtime.cpp
      src/features/ui/native/runtime_dom.cpp
      src/features/ui/native/system.cpp
      src/features/ui/native/system_documents.cpp
      src/features/ui/native/system_frame.cpp
      src/features/ui/native/system_input.cpp
      src/features/ui/native/system_interaction.cpp
      src/features/ui/native/system_reconciliation.cpp
      src/features/ui/native/development_loader.cpp
      src/features/ui/native/file_watcher.cpp
      src/features/ui/native/font_face.cpp
      src/features/ui/native/layout_engine.cpp
      src/features/ui/native/listener_registry.cpp
      src/features/ui/native/motion_engine.cpp
      src/features/ui/native/paint_engine.cpp
      src/features/ui/native/presentation_resources.cpp
      src/features/ui/native/presentation_builder.cpp
      src/features/ui/native/presentation_runtime.cpp
      src/features/ui/native/style_runtime.cpp
      src/features/ui/native/text_engine.cpp
      src/features/ui/native/transient_runtime.cpp
      src/features/ui/native/widget_paint.cpp
      src/features/ui/native/widget_runtime.cpp
      src/features/ui/native/svg_rasterizer.cpp
    )
    target_link_libraries(karma_features_ui_native
      PUBLIC
        karma_core
        karma_content
        karma_rendering_graphical
      # PRIVATE dependencies of a static target remain transitive link-only
      # requirements without exporting their compile usage to consumers.
      PRIVATE
        ${KARMA_YOGA_BUILD_LINK}
        ${KARMA_FREETYPE_BUILD_LINK}
        ${KARMA_HARFBUZZ_BUILD_LINK}
        ${KARMA_ICU_UC_BUILD_LINK}
        ${KARMA_ICU_I18N_BUILD_LINK}
        ${KARMA_LUNASVG_BUILD_LINK}
        $<INSTALL_INTERFACE:$<1:yoga::yogacore>>
        $<INSTALL_INTERFACE:$<1:Freetype::Freetype>>
        $<INSTALL_INTERFACE:$<1:harfbuzz::harfbuzz>>
        $<INSTALL_INTERFACE:$<1:ICU::uc>>
        $<INSTALL_INTERFACE:$<1:ICU::i18n>>
        $<INSTALL_INTERFACE:$<1:lunasvg::lunasvg>>
    )
    target_compile_definitions(karma_features_ui_native PUBLIC KARMA_ENABLE_NATIVE_UI)
    list(APPEND KARMA_INSTALL_TARGETS karma_features_ui_native)
  endif()

  set(KARMA_IMGUI_LINK_TARGET "")
  if (KARMA_ENABLE_IMGUI)
    set(KARMA_IMGUI_LINK_TARGET "${KARMA_IMGUI_TARGET}")
    if (NOT KARMA_IMGUI_LINK_TARGET AND DEFINED IMGUI_SOURCES)
      karma_add_static(karma_imgui_vendor
        ${IMGUI_SOURCES}
      )
      if (imgui_SOURCE_DIR)
        target_include_directories(karma_imgui_vendor
          PUBLIC
            $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include/karma/vendor/imgui>
        )
      endif()
      set_target_properties(karma_imgui_vendor PROPERTIES EXPORT_NAME imgui_vendor)
      list(APPEND KARMA_INSTALL_TARGETS karma_imgui_vendor)
      set(KARMA_IMGUI_LINK_TARGET karma_imgui_vendor)
    endif()
  endif()

  set(KARMA_RUNTIME_GRAPHICAL_SOURCES ${KARMA_RUNTIME_COMMON_SOURCES})
  if (KARMA_BUILD_DEBUG_UI)
    list(APPEND KARMA_RUNTIME_GRAPHICAL_SOURCES
      src/runtime/debug/debug_overlay.cpp
    )
  endif()

  karma_add_static(karma_runtime_graphical
    ${KARMA_RUNTIME_GRAPHICAL_SOURCES}
  )
  target_link_libraries(karma_runtime_graphical
    PUBLIC
      karma_core
      karma_world
      karma_rendering_graphical
      karma_media_graphical
      karma_simulation_animation
      karma_simulation_collision
      karma_simulation_physics
      karma_platform_network
      karma_features_network
      karma_platform_window_graphical
      karma_features_visual
  )
  if (KARMA_ENABLE_NATIVE_UI)
    target_link_libraries(karma_runtime_graphical PUBLIC karma_features_ui_native)
    target_compile_definitions(karma_runtime_graphical PUBLIC KARMA_ENABLE_NATIVE_UI)
  endif()
  if (KARMA_RENDER_BACKEND_DILIGENT)
    target_compile_definitions(karma_runtime_graphical PUBLIC KARMA_RENDER_BACKEND_DILIGENT)
  endif()
  if (KARMA_BUILD_DEBUG_UI)
    target_compile_definitions(karma_runtime_graphical PUBLIC KARMA_DEBUG_UI)
    if (KARMA_IMGUI_LINK_TARGET)
      target_link_libraries(karma_runtime_graphical PUBLIC ${KARMA_IMGUI_LINK_TARGET})
    endif()
    if (imgui_SOURCE_DIR)
      target_include_directories(karma_runtime_graphical PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>)
    endif()
  endif()
  if (KARMA_ENABLE_NAVIGATION)
    target_link_libraries(karma_runtime_graphical PUBLIC karma_simulation_navigation)
    target_compile_definitions(karma_runtime_graphical PUBLIC KARMA_ENABLE_NAVIGATION)
  endif()
  target_link_libraries(karma_runtime_graphical PUBLIC karma_content)
  list(APPEND KARMA_INSTALL_TARGETS karma_runtime_graphical)

  if (KARMA_ENABLE_IMGUI)
    karma_add_static(karma_features_ui_imgui
      src/features/ui/imgui/imgui_layer.cpp
    )
    target_link_libraries(karma_features_ui_imgui PUBLIC karma_core karma_runtime_graphical)
    if (KARMA_IMGUI_LINK_TARGET)
      target_link_libraries(karma_features_ui_imgui PUBLIC ${KARMA_IMGUI_LINK_TARGET})
    endif()
    if (imgui_SOURCE_DIR)
      target_include_directories(karma_features_ui_imgui
        PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>)
    endif()
    target_compile_definitions(karma_features_ui_imgui PUBLIC KARMA_ENABLE_IMGUI)
    list(APPEND KARMA_INSTALL_TARGETS karma_features_ui_imgui)
  endif()

  if (KARMA_ENABLE_RMLUI)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
      set(KARMA_RMLUI_BUILD_LINK "$<BUILD_LOCAL_INTERFACE:${KARMA_RMLUI_TARGET}>")
    else()
      set(KARMA_RMLUI_BUILD_LINK "$<BUILD_INTERFACE:${KARMA_RMLUI_TARGET}>")
    endif()
    karma_add_static(karma_features_ui_rmlui
      src/features/ui/rmlui/rmlui_layer.cpp
    )
    target_link_libraries(karma_features_ui_rmlui
      PUBLIC
        karma_core
        karma_runtime_graphical
        ${KARMA_RMLUI_BUILD_LINK}
        $<INSTALL_INTERFACE:$<1:RmlUi::Core>>
    )
    target_compile_definitions(karma_features_ui_rmlui PUBLIC KARMA_ENABLE_RMLUI)
    list(APPEND KARMA_INSTALL_TARGETS karma_features_ui_rmlui)
  endif()
endif()

if (KARMA_BUILD_SERVER_PROFILE)
  set(KARMA_SERVER_PROFILE_LIBS
    karma_features_network
    karma_platform_network
    karma_world
    karma_core
  )

  add_library(karma_server INTERFACE)
  add_library(karma::server ALIAS karma_server)
  karma_configure_interface(karma_server)
  target_link_libraries(karma_server INTERFACE ${KARMA_SERVER_PROFILE_LIBS})
  set_target_properties(karma_server PROPERTIES EXPORT_NAME server)
  list(APPEND KARMA_INSTALL_TARGETS karma_server)
endif()

if (KARMA_BUILD_HEADLESS_PROFILE)
  set(KARMA_HEADLESS_PROFILE_LIBS
    karma_runtime_headless
    karma_features_visual
    karma_features_network
    karma_platform_window_headless
    karma_platform_network
    karma_content
    karma_media_headless
    karma_simulation_physics
    karma_simulation_collision
    karma_simulation_animation
    karma_rendering_headless
    karma_world
    karma_core
  )
  if (KARMA_ENABLE_NAVIGATION)
    list(APPEND KARMA_HEADLESS_PROFILE_LIBS karma_simulation_navigation)
  endif()

  add_library(karma_headless INTERFACE)
  add_library(karma::headless ALIAS karma_headless)
  karma_configure_interface(karma_headless)
  target_link_libraries(karma_headless INTERFACE ${KARMA_HEADLESS_PROFILE_LIBS})
  target_compile_definitions(karma_headless INTERFACE KARMA_HEADLESS)
  if (KARMA_ENABLE_NAVIGATION)
    target_compile_definitions(karma_headless INTERFACE KARMA_ENABLE_NAVIGATION)
  endif()
  set_target_properties(karma_headless PROPERTIES EXPORT_NAME headless)
  list(APPEND KARMA_INSTALL_TARGETS karma_headless)
endif()

if (KARMA_BUILD_GRAPHICAL_PROFILE)
  set(KARMA_GRAPHICAL_PROFILE_LIBS
    karma_runtime_graphical
    karma_features_visual
    karma_features_network
    karma_platform_window_graphical
    karma_platform_network
    karma_media_graphical
    karma_simulation_physics
    karma_simulation_collision
    karma_simulation_animation
    karma_rendering_graphical
    karma_world
    karma_core
  )
  if (KARMA_ENABLE_NATIVE_UI)
    list(APPEND KARMA_GRAPHICAL_PROFILE_LIBS karma_features_ui_native)
  endif()
  if (KARMA_ENABLE_IMGUI)
    list(APPEND KARMA_GRAPHICAL_PROFILE_LIBS karma_features_ui_imgui)
  endif()
  if (KARMA_ENABLE_NAVIGATION)
    list(APPEND KARMA_GRAPHICAL_PROFILE_LIBS karma_simulation_navigation)
  endif()
  list(APPEND KARMA_GRAPHICAL_PROFILE_LIBS karma_content)
  if (KARMA_ENABLE_RMLUI)
    list(APPEND KARMA_GRAPHICAL_PROFILE_LIBS karma_features_ui_rmlui)
  endif()

  add_library(karma_graphical INTERFACE)
  add_library(karma::graphical ALIAS karma_graphical)
  karma_configure_interface(karma_graphical)
  target_link_libraries(karma_graphical INTERFACE ${KARMA_GRAPHICAL_PROFILE_LIBS})
  if (KARMA_ENABLE_NATIVE_UI)
    target_compile_definitions(karma_graphical INTERFACE KARMA_ENABLE_NATIVE_UI)
  endif()
  if (KARMA_RENDER_BACKEND_DILIGENT)
    target_compile_definitions(karma_graphical INTERFACE KARMA_RENDER_BACKEND_DILIGENT)
  endif()
  if (KARMA_BUILD_DEBUG_UI)
    target_compile_definitions(karma_graphical INTERFACE KARMA_DEBUG_UI)
  endif()
  if (KARMA_ENABLE_NAVIGATION)
    target_compile_definitions(karma_graphical INTERFACE KARMA_ENABLE_NAVIGATION)
  endif()
  set_target_properties(karma_graphical PROPERTIES EXPORT_NAME graphical)
  list(APPEND KARMA_INSTALL_TARGETS karma_graphical)

  add_library(karma INTERFACE)
  add_library(karma::karma ALIAS karma)
  karma_configure_interface(karma)
  target_link_libraries(karma INTERFACE karma_graphical)
  set_target_properties(karma PROPERTIES EXPORT_NAME karma)
  list(APPEND KARMA_INSTALL_TARGETS karma)
endif()
