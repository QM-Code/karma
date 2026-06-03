set(KARMA_SOURCES
  src/runtime/app/engine_app.cpp
  src/runtime/app/ui_context.cpp
  src/runtime/scene/scene_helpers.cpp
  src/simulation/animation/animation_clip.cpp
  src/simulation/animation/animation_system.cpp
  src/simulation/animation/cpu_skinning_system.cpp
  src/world/components/animation_player.cpp
  src/world/components/transform.cpp
  src/world/ecs/collider_queries.cpp
  src/content/importers/glb_scene_import.cpp
  src/world/scene/transform_hierarchy.cpp
  src/media/audio/audio.cpp
  src/media/audio/backend_factory.cpp
  src/media/audio/audio_system.cpp
  src/simulation/collision/collision_event_system.cpp
  src/platform/network/transport_factory.cpp
  src/platform/network/enet_transport.cpp
  src/runtime/input/input_system.cpp
  src/rendering/renderer/backend_factory.cpp
  src/rendering/renderer/camera_picking.cpp
  src/rendering/renderer/device.cpp
  src/rendering/renderer/render_system.cpp
  src/platform/window/window_factory.cpp
  src/simulation/physics/backend_factory.cpp
  src/simulation/physics/rigid_body.cpp
  src/simulation/physics/static_body.cpp
  src/simulation/physics/player_controller.cpp
  src/simulation/physics/physics_world.cpp
  src/simulation/physics/physics_system.cpp
  src/content/image/stb_image.cpp
  src/content/geometry/mesh_loader.cpp
  src/features/visual/beams/beam_path_runtime_module.cpp
  src/features/visual/beams/beam_path_system.cpp
  src/features/visual/lights/light_pulse_system.cpp
  src/features/visual/particles/effect_library.cpp
  src/features/visual/particles/particle_system.cpp
  src/content/prefabs/component_serializer_registry.cpp
  src/content/prefabs/prefab_resources.cpp
  src/content/prefabs/prefab_runtime.cpp
  src/content/prefabs/prefab_registry.cpp
  src/features/visual/volumes/volume_sphere_runtime_module.cpp
  src/features/visual/volumes/volume_sphere_system.cpp
)

if (NOT KARMA_HEADLESS)
  list(APPEND KARMA_SOURCES
    src/features/ui/imgui/imgui_layer.cpp
  )
endif()

if (KARMA_ENABLE_RMLUI)
  list(APPEND KARMA_SOURCES
    src/features/ui/rmlui/rmlui_layer.cpp
  )
endif()

if (KARMA_ENABLE_NAVIGATION)
  list(APPEND KARMA_SOURCES
    src/simulation/navigation/nav_geometry.cpp
    src/simulation/navigation/nav_mesh.cpp
    src/simulation/navigation/navigation_diagnostics.cpp
    src/simulation/navigation/navigation_system.cpp
  )
endif()

if (KARMA_BUILD_DEBUG_UI)
  list(APPEND KARMA_SOURCES
    src/runtime/debug/debug_overlay.cpp
  )
endif()

if (NOT KARMA_HEADLESS AND DEFINED IMGUI_SOURCES)
  list(APPEND KARMA_SOURCES
    ${IMGUI_SOURCES}
  )
endif()

if (KARMA_RENDER_BACKEND_DILIGENT)
  list(APPEND KARMA_SOURCES
    src/rendering/renderer/backends/diligent/backend_common.cpp
    src/rendering/renderer/backends/diligent/backend_init.cpp
    src/rendering/renderer/backends/diligent/backend_render.cpp
    src/rendering/renderer/backends/diligent/backend_textures.cpp
    src/rendering/renderer/backends/diligent/backend_ui.cpp
    src/rendering/renderer/backends/diligent/access.cpp
    src/rendering/renderer/backends/diligent/passes/camera_override.cpp
    src/rendering/renderer/backends/diligent/passes/environment.cpp
    src/rendering/renderer/backends/diligent/passes/frame.cpp
    src/rendering/renderer/backends/diligent/passes/forward.cpp
    src/rendering/renderer/backends/diligent/passes/line.cpp
    src/rendering/renderer/backends/diligent/passes/particle_draw.cpp
    src/rendering/renderer/backends/diligent/passes/particles.cpp
    src/rendering/renderer/backends/diligent/passes/render_state.cpp
    src/rendering/renderer/backends/diligent/passes/shadows.cpp
    src/rendering/renderer/backends/diligent/resources/materials.cpp
    src/rendering/renderer/backends/diligent/resources/meshes.cpp
    src/rendering/renderer/backends/diligent/resources/render_targets.cpp
    src/rendering/renderer/backends/diligent/resources/textures.cpp
  )
endif()

if (KARMA_WINDOW_BACKEND_SDL)
  list(APPEND KARMA_SOURCES
    src/platform/window/backends/window_sdl.cpp
  )
elseif (KARMA_WINDOW_BACKEND_GLFW)
  list(APPEND KARMA_SOURCES
    src/platform/window/backends/window_glfw.cpp
  )
endif()

if (KARMA_AUDIO_BACKEND_MINIAUDIO)
  list(APPEND KARMA_SOURCES
    src/media/audio/backends/miniaudio/backend.cpp
    src/media/audio/backends/miniaudio/clip.cpp
  )
endif()

if (KARMA_AUDIO_BACKEND_SDL)
  list(APPEND KARMA_SOURCES
    src/media/audio/backends/sdl/backend.cpp
    src/media/audio/backends/sdl/clip.cpp
  )
endif()

if (KARMA_PHYSICS_BACKEND_JOLT AND KARMA_PHYSICS_BACKEND_BULLET)
  message(FATAL_ERROR "Choose only one physics backend: Jolt or Bullet.")
endif()

if (KARMA_PHYSICS_BACKEND_JOLT)
  list(APPEND KARMA_SOURCES
    src/simulation/physics/backends/jolt/physics_world_jolt.cpp
    src/simulation/physics/backends/jolt/rigid_body_jolt.cpp
    src/simulation/physics/backends/jolt/player_controller_jolt.cpp
    src/simulation/physics/backends/jolt/static_body_jolt.cpp
  )
endif()

if (KARMA_PHYSICS_BACKEND_BULLET)
  list(APPEND KARMA_SOURCES
    src/simulation/physics/backends/bullet/physics_world_bullet.cpp
    src/simulation/physics/backends/bullet/rigid_body_bullet.cpp
    src/simulation/physics/backends/bullet/player_controller_bullet.cpp
    src/simulation/physics/backends/bullet/static_body_bullet.cpp
  )
endif()

add_library(karma STATIC ${KARMA_SOURCES})

target_include_directories(karma
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${KARMA_EXTRA_INCLUDE_DIRS}
)

if (KARMA_RENDER_BACKEND_DILIGENT AND diligentcore_SOURCE_DIR)
  target_include_directories(karma PUBLIC ${diligentcore_SOURCE_DIR})
  target_include_directories(karma PUBLIC ${diligentcore_SOURCE_DIR}/Graphics/GraphicsTools/interface)
endif()

target_compile_features(karma PUBLIC cxx_std_20)

if (KARMA_HEADLESS)
  target_compile_definitions(karma PUBLIC KARMA_HEADLESS)
endif()
if (KARMA_RENDER_BACKEND_DILIGENT)
  target_compile_definitions(karma PUBLIC BZ3_RENDER_BACKEND_DILIGENT)
endif()

if (KARMA_WINDOW_BACKEND_SDL)
  target_compile_definitions(karma PUBLIC BZ3_WINDOW_BACKEND_SDL)
endif()

if (KARMA_PHYSICS_BACKEND_JOLT)
  target_compile_definitions(karma PUBLIC KARMA_PHYSICS_BACKEND_JOLT)
endif()

if (KARMA_PHYSICS_BACKEND_BULLET)
  target_compile_definitions(karma PUBLIC KARMA_PHYSICS_BACKEND_BULLET)
endif()
if (KARMA_AUDIO_BACKEND_MINIAUDIO)
  target_compile_definitions(karma PUBLIC KARMA_AUDIO_BACKEND_MINIAUDIO)
endif()
if (KARMA_AUDIO_BACKEND_SDL)
  target_compile_definitions(karma PUBLIC KARMA_AUDIO_BACKEND_SDL)
endif()
if (KARMA_NETWORK_BACKEND_ENET)
  target_compile_definitions(karma PUBLIC KARMA_NETWORK_BACKEND_ENET)
endif()
if (KARMA_BUILD_DEBUG_UI)
  target_compile_definitions(karma PUBLIC KARMA_DEBUG_UI)
endif()
if (KARMA_ENABLE_NAVIGATION)
  target_compile_definitions(karma PUBLIC KARMA_ENABLE_NAVIGATION)
endif()

target_link_libraries(karma
  PUBLIC
    ${KARMA_PLATFORM_LINK_LIBS}
    ${KARMA_RENDER_LINK_LIBS}
    ${KARMA_PHYSICS_LINK_LIBS}
    ${KARMA_AUDIO_LINK_LIBS}
    ${KARMA_NETWORK_LINK_LIBS}
    ${KARMA_NAVIGATION_LINK_LIBS}
    ${KARMA_EXTRA_LINK_LIBS}
)

if (NOT KARMA_HEADLESS)
  if (KARMA_IMGUI_TARGET)
    target_link_libraries(karma PUBLIC ${KARMA_IMGUI_TARGET})
  endif()
  if (imgui_SOURCE_DIR)
    target_include_directories(karma PUBLIC ${imgui_SOURCE_DIR})
  endif()
endif()

if (KARMA_ENABLE_RMLUI)
  target_link_libraries(karma PUBLIC ${KARMA_RMLUI_TARGET})
endif()
