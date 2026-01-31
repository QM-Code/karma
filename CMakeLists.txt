cmake_minimum_required(VERSION 3.20)

project(karma LANGUAGES CXX C)

set(BUILD_TESTING ON CACHE BOOL "Disable CTest targets for Karma deps" FORCE)

option(KARMA_WINDOW_BACKEND_GLFW "Use GLFW window backend" ON)
option(KARMA_WINDOW_BACKEND_SDL "Use SDL window backend" OFF)
option(KARMA_RENDER_BACKEND_DILIGENT "Use Diligent renderer backend" ON)
option(KARMA_AUDIO_BACKEND_MINIAUDIO "Use miniaudio backend" ON)
option(KARMA_AUDIO_BACKEND_SDL "Use SDL audio backend" OFF)
option(KARMA_NETWORK_BACKEND_ENET "Use ENet network backend" ON)
option(KARMA_PHYSICS_BACKEND_JOLT "Use Jolt physics backend" ON)
option(KARMA_PHYSICS_BACKEND_BULLET "Use Bullet physics backend" OFF)
option(KARMA_FETCH_DEPS "Fetch third-party dependencies if missing" ON)
option(KARMA_BUILD_IMGUI_DEMO "Build ImGui UI demo" ON)
option(KARMA_BUILD_RMLUI_DEMO "Build RmlUi UI demo" ON)
set(KARMA_DILIGENT_TAG "v2.5.5" CACHE STRING "DiligentCore git tag/branch to fetch")

if (KARMA_WINDOW_BACKEND_GLFW AND KARMA_WINDOW_BACKEND_SDL)
  message(FATAL_ERROR "Choose only one window backend: GLFW or SDL.")
endif()
if (KARMA_AUDIO_BACKEND_MINIAUDIO AND KARMA_AUDIO_BACKEND_SDL)
  message(FATAL_ERROR "Choose only one audio backend: miniaudio or SDL.")
endif()

set(KARMA_PLATFORM_LINK_LIBS "" CACHE STRING "Platform backend libraries")
set(KARMA_RENDER_LINK_LIBS "" CACHE STRING "Renderer backend libraries")
set(KARMA_PHYSICS_LINK_LIBS "" CACHE STRING "Physics backend libraries")
set(KARMA_AUDIO_LINK_LIBS "" CACHE STRING "Audio backend libraries")
set(KARMA_NETWORK_LINK_LIBS "" CACHE STRING "Network backend libraries")
set(KARMA_EXTRA_LINK_LIBS "" CACHE STRING "Additional Karma libraries")
set(KARMA_EXTRA_INCLUDE_DIRS "" CACHE STRING "Additional Karma include paths")

include(FetchContent)

find_package(glm QUIET)
if (NOT TARGET glm::glm AND KARMA_FETCH_DEPS)
  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
  )
  FetchContent_MakeAvailable(glm)
endif()

if (KARMA_BUILD_IMGUI_DEMO)
  find_package(imgui CONFIG QUIET)
  if (TARGET imgui::imgui)
    set(KARMA_IMGUI_TARGET imgui::imgui)
  elseif (KARMA_FETCH_DEPS)
    FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.92.5
    )
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_SOURCES
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )
    set(KARMA_IMGUI_TARGET "")
  else()
    message(FATAL_ERROR "KARMA_BUILD_IMGUI_DEMO=ON but ImGui not found. Provide imgui or enable KARMA_FETCH_DEPS.")
  endif()
endif()

if (KARMA_BUILD_RMLUI_DEMO)
  find_package(RmlUi CONFIG QUIET)
  if (TARGET RmlUi::Core)
    set(KARMA_RMLUI_TARGET RmlUi::Core)
  elseif (KARMA_FETCH_DEPS)
    set(RMLUI_SVG_PLUGIN ON CACHE BOOL "" FORCE)
    set(RMLUI_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(RMLUI_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
    set(RMLUI_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      lunasvg
      GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
      GIT_TAG v2.4.0
    )
    FetchContent_MakeAvailable(lunasvg)
    FetchContent_Declare(
      rmlui
      GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
      GIT_TAG 6.0
    )
    FetchContent_MakeAvailable(rmlui)
    if (TARGET RmlUi::Core)
      set(KARMA_RMLUI_TARGET RmlUi::Core)
    elseif (TARGET rmlui_core)
      set(KARMA_RMLUI_TARGET rmlui_core)
    else()
      set(KARMA_RMLUI_TARGET "")
    endif()
  else()
    message(FATAL_ERROR "KARMA_BUILD_RMLUI_DEMO=ON but RmlUi not found. Provide RmlUi or enable KARMA_FETCH_DEPS.")
  endif()
endif()

if (TARGET glm::glm)
  list(APPEND KARMA_EXTRA_LINK_LIBS glm::glm)
endif()

find_package(spdlog QUIET)
if (NOT TARGET spdlog::spdlog AND KARMA_FETCH_DEPS)
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
  )
  FetchContent_MakeAvailable(spdlog)
endif()
if (TARGET spdlog::spdlog)
  list(APPEND KARMA_EXTRA_LINK_LIBS spdlog::spdlog)
endif()

if (KARMA_AUDIO_BACKEND_MINIAUDIO AND KARMA_FETCH_DEPS)
  FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
  )
  FetchContent_GetProperties(miniaudio)
  if (NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
  endif()
  list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${miniaudio_SOURCE_DIR})
endif()

if (KARMA_WINDOW_BACKEND_SDL)
  find_package(SDL3 QUIET)
  if (NOT TARGET SDL3::SDL3 AND KARMA_FETCH_DEPS)
    FetchContent_Declare(
      SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.1.8
    )
    FetchContent_MakeAvailable(SDL3)
  endif()
  if (TARGET SDL3::SDL3)
    list(APPEND KARMA_PLATFORM_LINK_LIBS SDL3::SDL3)
  endif()
endif()
if (KARMA_AUDIO_BACKEND_SDL)
  if (TARGET SDL3::SDL3)
    list(APPEND KARMA_AUDIO_LINK_LIBS SDL3::SDL3)
  endif()
endif()

if (KARMA_RENDER_BACKEND_DILIGENT)
  find_package(DiligentCore QUIET)
  if (NOT TARGET Diligent-GraphicsEngineOpenGL-shared AND KARMA_FETCH_DEPS)
    set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_VULKAN OFF CACHE BOOL "" FORCE)
    set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_OPENGLES ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_METAL ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)
    set(DILIGENT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(DILIGENT_BUILD_FX OFF CACHE BOOL "" FORCE)
    set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      diligentcore
      GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
      GIT_TAG ${KARMA_DILIGENT_TAG}
      SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/DiligentCore
    )
    FetchContent_MakeAvailable(diligentcore)
  endif()
  set(KARMA_DILIGENT_TARGET "")
  if (TARGET Diligent-GraphicsEngineVk-shared)
    set(KARMA_DILIGENT_TARGET Diligent-GraphicsEngineVk-shared)
  elseif (TARGET Diligent-GraphicsEngineVk-static)
    set(KARMA_DILIGENT_TARGET Diligent-GraphicsEngineVk-static)
  endif()
  if (KARMA_DILIGENT_TARGET)
    if (TARGET Diligent-BuildSettings)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-BuildSettings)
    endif()
    list(APPEND KARMA_RENDER_LINK_LIBS ${KARMA_DILIGENT_TARGET})
  else()
    message(FATAL_ERROR "Karma: Diligent backend enabled but Diligent Vulkan target not found.")
  endif()
  if (diligentcore_SOURCE_DIR)
    list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${diligentcore_SOURCE_DIR})
  endif()
endif()

if (KARMA_NETWORK_BACKEND_ENET)
  find_package(enet QUIET)
  if (TARGET enet_static)
    list(APPEND KARMA_NETWORK_LINK_LIBS enet_static)
  elseif (KARMA_FETCH_DEPS)
    FetchContent_Declare(
      enet
      GIT_REPOSITORY https://github.com/zpl-c/enet.git
      GIT_TAG master
    )
    set(ENET_TEST OFF CACHE BOOL "" FORCE)
    set(ENET_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(enet)
    if (TARGET enet_static)
      list(APPEND KARMA_NETWORK_LINK_LIBS enet_static)
    endif()
  endif()
  if (enet_SOURCE_DIR)
    list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${enet_SOURCE_DIR}/include)
  endif()
endif()

find_package(assimp QUIET)
if (NOT TARGET assimp::assimp AND KARMA_FETCH_DEPS)
  set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.3.1
  )
  FetchContent_MakeAvailable(assimp)
endif()
if (TARGET assimp::assimp)
  list(APPEND KARMA_EXTRA_LINK_LIBS assimp::assimp)
elseif (TARGET assimp)
  list(APPEND KARMA_EXTRA_LINK_LIBS assimp)
endif()

if (KARMA_WINDOW_BACKEND_GLFW)
  if (NOT TARGET glfw AND NOT TARGET glfw3::glfw)
    find_package(glfw3 QUIET)
  endif()
  if (NOT TARGET glfw AND NOT TARGET glfw3::glfw AND KARMA_FETCH_DEPS)
    FetchContent_Declare(
      glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG 3.3.8
    )
    FetchContent_MakeAvailable(glfw)
  endif()
  if (TARGET glfw)
    list(APPEND KARMA_PLATFORM_LINK_LIBS glfw)
  elseif(TARGET glfw3::glfw)
    list(APPEND KARMA_PLATFORM_LINK_LIBS glfw3::glfw)
  endif()
endif()

if (KARMA_PHYSICS_BACKEND_BULLET)
  find_package(Bullet QUIET)
  if (TARGET Bullet::Bullet)
    list(APPEND KARMA_PHYSICS_LINK_LIBS Bullet::Bullet)
  elseif (BULLET_LIBRARIES)
    list(APPEND KARMA_PHYSICS_LINK_LIBS ${BULLET_LIBRARIES})
  elseif (KARMA_FETCH_DEPS)
    message(WARNING "Karma: Bullet backend enabled but Bullet not found. Set KARMA_PHYSICS_LINK_LIBS manually.")
  endif()
endif()

if (KARMA_PHYSICS_BACKEND_JOLT)
  find_package(Jolt QUIET)
  if (NOT TARGET Jolt::Jolt AND KARMA_FETCH_DEPS)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(DISABLE_CUSTOM_ALLOCATOR ON CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      JoltPhysics
      GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
      GIT_TAG v5.1.0
      SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(JoltPhysics)
  endif()
  if (TARGET Jolt::Jolt)
    list(APPEND KARMA_PHYSICS_LINK_LIBS Jolt::Jolt)
  elseif (TARGET Jolt)
    list(APPEND KARMA_PHYSICS_LINK_LIBS Jolt)
  else()
    message(WARNING "Karma: Jolt backend enabled but Jolt not found. Set KARMA_PHYSICS_LINK_LIBS manually.")
  endif()
endif()

set(KARMA_SOURCES
  src/app/engine_app.cpp
  src/app/ui_context.cpp
  src/components/transform.cpp
  src/audio/audio.cpp
  src/audio/backend_factory.cpp
  src/audio/audio_system.cpp
  src/network/transport_factory.cpp
  src/network/enet_transport.cpp
  src/input/input_system.cpp
  src/renderer/backend_factory.cpp
  src/renderer/device.cpp
  src/renderer/render_system.cpp
  src/platform/window_factory.cpp
  src/physics/backend_factory.cpp
  src/physics/rigid_body.cpp
  src/physics/static_body.cpp
  src/physics/player_controller.cpp
  src/physics/physics_world.cpp
  src/physics/physics_system.cpp
  src/geometry/mesh_loader.cpp
)

if (KARMA_RENDER_BACKEND_DILIGENT)
  list(APPEND KARMA_SOURCES
    src/renderer/backends/diligent/backend_common.cpp
    src/renderer/backends/diligent/backend_init.cpp
    src/renderer/backends/diligent/backend_mesh.cpp
    src/renderer/backends/diligent/backend_render.cpp
    src/renderer/backends/diligent/backend_textures.cpp
    src/renderer/backends/diligent/backend_ui.cpp
    src/renderer/backends/diligent/stb_image.cpp
  )
endif()

if (KARMA_WINDOW_BACKEND_SDL)
  list(APPEND KARMA_SOURCES
    src/platform/backends/window_sdl.cpp
  )
else()
  list(APPEND KARMA_SOURCES
    src/platform/backends/window_glfw.cpp
  )
endif()

if (KARMA_AUDIO_BACKEND_MINIAUDIO)
  list(APPEND KARMA_SOURCES
    src/audio/backends/miniaudio/backend.cpp
    src/audio/backends/miniaudio/clip.cpp
  )
endif()

if (KARMA_AUDIO_BACKEND_SDL)
  list(APPEND KARMA_SOURCES
    src/audio/backends/sdl/backend.cpp
    src/audio/backends/sdl/clip.cpp
  )
endif()

if (KARMA_PHYSICS_BACKEND_JOLT AND KARMA_PHYSICS_BACKEND_BULLET)
  message(FATAL_ERROR "Choose only one physics backend: Jolt or Bullet.")
endif()

if (KARMA_PHYSICS_BACKEND_JOLT)
  list(APPEND KARMA_SOURCES
    src/physics/backends/jolt/physics_world_jolt.cpp
    src/physics/backends/jolt/rigid_body_jolt.cpp
    src/physics/backends/jolt/player_controller_jolt.cpp
    src/physics/backends/jolt/static_body_jolt.cpp
  )
endif()

if (KARMA_PHYSICS_BACKEND_BULLET)
  list(APPEND KARMA_SOURCES
    src/physics/backends/bullet/physics_world_bullet.cpp
    src/physics/backends/bullet/rigid_body_bullet.cpp
    src/physics/backends/bullet/player_controller_bullet.cpp
    src/physics/backends/bullet/static_body_bullet.cpp
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

target_link_libraries(karma
  PUBLIC
    ${KARMA_PLATFORM_LINK_LIBS}
    ${KARMA_RENDER_LINK_LIBS}
    ${KARMA_PHYSICS_LINK_LIBS}
    ${KARMA_AUDIO_LINK_LIBS}
    ${KARMA_NETWORK_LINK_LIBS}
    ${KARMA_EXTRA_LINK_LIBS}
)

add_executable(karma_example
  examples/main.cpp
)

target_link_libraries(karma_example PRIVATE karma)

if (KARMA_BUILD_IMGUI_DEMO)
  add_executable(karma_imgui_ui_demo
    examples/imgui_ui_demo.cpp
    ${IMGUI_SOURCES}
  )
  target_link_libraries(karma_imgui_ui_demo PRIVATE karma ${KARMA_IMGUI_TARGET})
  if (imgui_SOURCE_DIR)
    target_include_directories(karma_imgui_ui_demo PRIVATE ${imgui_SOURCE_DIR})
  endif()
endif()

if (KARMA_BUILD_RMLUI_DEMO)
  add_executable(karma_rmlui_ui_demo
    examples/rmlui_ui_demo.cpp
  )
  target_link_libraries(karma_rmlui_ui_demo PRIVATE karma ${KARMA_RMLUI_TARGET})
endif()

add_executable(karma_network_demo
  examples/network_demo.cpp
)

target_link_libraries(karma_network_demo PRIVATE karma)
