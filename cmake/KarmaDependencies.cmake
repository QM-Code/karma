set(KARMA_PLATFORM_LINK_LIBS "" CACHE STRING "Platform backend libraries")
set(KARMA_RENDER_LINK_LIBS "" CACHE STRING "Renderer backend libraries")
set(KARMA_PHYSICS_LINK_LIBS "" CACHE STRING "Physics backend libraries")
set(KARMA_AUDIO_LINK_LIBS "" CACHE STRING "Audio backend libraries")
set(KARMA_NETWORK_LINK_LIBS "" CACHE STRING "Network backend libraries")
set(KARMA_NAVIGATION_LINK_LIBS "" CACHE STRING "Navigation libraries")
set(KARMA_EXTRA_LINK_LIBS "" CACHE STRING "Additional Karma libraries")
set(KARMA_EXTRA_INCLUDE_DIRS "" CACHE STRING "Additional Karma include paths")

find_package(Threads REQUIRED)
list(APPEND KARMA_EXTRA_LINK_LIBS Threads::Threads)

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

# `karma_example` and `karma_collision_events_example` both use ImGui-backed
# UI overlays, so any non-headless build needs ImGui even when the standalone
# ImGui demo target is disabled.
if (NOT KARMA_HEADLESS)
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
    message(FATAL_ERROR "ImGui is required but not found. Provide imgui or enable KARMA_FETCH_DEPS.")
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
    if (TARGET Diligent-GraphicsTools)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-GraphicsTools)
    endif()
    if (TARGET Diligent-Common)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-Common)
    endif()
    if (TARGET Diligent-TargetPlatform)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-TargetPlatform)
    endif()
  else()
    message(FATAL_ERROR "Karma: Diligent backend enabled but Diligent Vulkan target not found.")
  endif()
  if (diligentcore_SOURCE_DIR)
    list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${diligentcore_SOURCE_DIR})
  endif()

  if (TARGET xxhash)
    set(KARMA_XXHASH_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/diligentcore-build/ThirdParty/xxHash/cmake_unofficial/libxxhash.a")
    set(KARMA_XXHASH_OBJECT "${CMAKE_BINARY_DIR}/_deps/diligentcore-build/ThirdParty/xxHash/cmake_unofficial/CMakeFiles/xxhash.dir/__/xxhash.c.o")
    set(KARMA_XXHASH_SOURCE "${CMAKE_BINARY_DIR}/_deps/DiligentCore/ThirdParty/xxHash/xxhash.c")
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

if (KARMA_ENABLE_NAVIGATION)
  find_package(RecastNavigation QUIET)
  if (NOT TARGET Recast AND KARMA_FETCH_DEPS)
    set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
    set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      recastnavigation
      GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
      GIT_TAG v1.6.0
    )
    FetchContent_MakeAvailable(recastnavigation)
  endif()
  if (TARGET Recast AND TARGET Detour)
    list(APPEND KARMA_NAVIGATION_LINK_LIBS Recast Detour)
    if (TARGET DebugUtils)
      list(APPEND KARMA_NAVIGATION_LINK_LIBS DebugUtils)
    endif()
  else()
    message(FATAL_ERROR "Karma: navigation enabled but Recast/Detour targets were not found.")
  endif()
endif()
