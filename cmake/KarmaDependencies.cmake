set(KARMA_PLATFORM_LINK_LIBS "" CACHE STRING "Platform backend libraries")
set(KARMA_RENDER_LINK_LIBS "" CACHE STRING "Renderer backend libraries")
set(KARMA_PHYSICS_LINK_LIBS "" CACHE STRING "Physics backend libraries")
set(KARMA_AUDIO_LINK_LIBS "" CACHE STRING "Audio backend libraries")
set(KARMA_NETWORK_LINK_LIBS "" CACHE STRING "Network backend libraries")
set(KARMA_NAVIGATION_LINK_LIBS "" CACHE STRING "Navigation libraries")
set(KARMA_EXTRA_LINK_LIBS "" CACHE STRING "Additional Karma libraries")
set(KARMA_EXTRA_INCLUDE_DIRS "" CACHE STRING "Additional Karma include paths")
set(KARMA_INSTALL_LINK_LIBS "")

find_package(Threads REQUIRED)
list(APPEND KARMA_EXTRA_LINK_LIBS Threads::Threads)
list(APPEND KARMA_INSTALL_LINK_LIBS Threads::Threads)

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
if (NOT TARGET glm::glm)
  message(FATAL_ERROR "glm is required but not found. Provide glm or enable KARMA_FETCH_DEPS.")
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

if (KARMA_ENABLE_RMLUI)
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
    message(FATAL_ERROR "KARMA_ENABLE_RMLUI=ON but RmlUi not found. Provide RmlUi or enable KARMA_FETCH_DEPS.")
  endif()
  if (NOT KARMA_RMLUI_TARGET)
    message(FATAL_ERROR "KARMA_ENABLE_RMLUI=ON but no usable RmlUi target was found.")
  endif()
  list(APPEND KARMA_INSTALL_LINK_LIBS RmlUi::Core)
endif()

if (TARGET glm::glm)
  list(APPEND KARMA_EXTRA_LINK_LIBS glm::glm)
  list(APPEND KARMA_INSTALL_LINK_LIBS glm::glm)
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
  list(APPEND KARMA_INSTALL_LINK_LIBS spdlog::spdlog)
else()
  message(FATAL_ERROR "spdlog is required but not found. Provide spdlog or enable KARMA_FETCH_DEPS.")
endif()

find_package(nlohmann_json CONFIG QUIET)
if (NOT TARGET nlohmann_json::nlohmann_json AND KARMA_FETCH_DEPS)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()
if (TARGET nlohmann_json::nlohmann_json)
  list(APPEND KARMA_EXTRA_LINK_LIBS nlohmann_json::nlohmann_json)
  list(APPEND KARMA_INSTALL_LINK_LIBS nlohmann_json::nlohmann_json)
else()
  message(FATAL_ERROR "nlohmann_json is required but not found. Provide nlohmann_json or enable KARMA_FETCH_DEPS.")
endif()

if (KARMA_AUDIO_BACKEND_MINIAUDIO)
  find_path(KARMA_MINIAUDIO_INCLUDE_DIR miniaudio.h)
  if (KARMA_MINIAUDIO_INCLUDE_DIR)
    list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${KARMA_MINIAUDIO_INCLUDE_DIR})
  elseif (KARMA_FETCH_DEPS)
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
  else()
    message(FATAL_ERROR "miniaudio backend enabled but miniaudio.h was not found. Provide miniaudio or enable KARMA_FETCH_DEPS.")
  endif()
endif()

if (KARMA_WINDOW_BACKEND_SDL OR KARMA_AUDIO_BACKEND_SDL)
  find_package(SDL3 QUIET)
  if (NOT TARGET SDL3::SDL3 AND KARMA_FETCH_DEPS)
    FetchContent_Declare(
      SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.1.8
    )
    FetchContent_MakeAvailable(SDL3)
  endif()
  if (NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3 backend enabled but SDL3 was not found. Provide SDL3 or enable KARMA_FETCH_DEPS.")
  endif()
endif()
if (KARMA_WINDOW_BACKEND_SDL)
  if (TARGET SDL3::SDL3)
    list(APPEND KARMA_PLATFORM_LINK_LIBS SDL3::SDL3)
    list(APPEND KARMA_INSTALL_LINK_LIBS SDL3::SDL3)
  endif()
endif()
if (KARMA_AUDIO_BACKEND_SDL)
  if (TARGET SDL3::SDL3)
    list(APPEND KARMA_AUDIO_LINK_LIBS SDL3::SDL3)
    if (NOT SDL3::SDL3 IN_LIST KARMA_INSTALL_LINK_LIBS)
      list(APPEND KARMA_INSTALL_LINK_LIBS SDL3::SDL3)
    endif()
  endif()
endif()

if (KARMA_RENDER_BACKEND_DILIGENT)
  find_package(DiligentCore QUIET)
  if (NOT TARGET Diligent-GraphicsEngineVk-shared AND NOT TARGET Diligent-GraphicsEngineVk-static AND KARMA_FETCH_DEPS)
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
      list(APPEND KARMA_INSTALL_LINK_LIBS Diligent-BuildSettings)
    endif()
    list(APPEND KARMA_RENDER_LINK_LIBS ${KARMA_DILIGENT_TARGET})
    list(APPEND KARMA_INSTALL_LINK_LIBS ${KARMA_DILIGENT_TARGET})
    if (TARGET Diligent-GraphicsTools)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-GraphicsTools)
      list(APPEND KARMA_INSTALL_LINK_LIBS Diligent-GraphicsTools)
    endif()
    if (TARGET Diligent-Common)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-Common)
      list(APPEND KARMA_INSTALL_LINK_LIBS Diligent-Common)
    endif()
    if (TARGET Diligent-TargetPlatform)
      list(APPEND KARMA_RENDER_LINK_LIBS Diligent-TargetPlatform)
      list(APPEND KARMA_INSTALL_LINK_LIBS Diligent-TargetPlatform)
    endif()
  else()
    message(FATAL_ERROR "Karma: Diligent backend enabled but Diligent Vulkan target not found.")
  endif()
  if (diligentcore_SOURCE_DIR)
    list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${diligentcore_SOURCE_DIR})
  endif()

  if (KARMA_DILIGENT_REPACK_XXHASH AND TARGET xxhash)
    set(KARMA_XXHASH_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/diligentcore-build/ThirdParty/xxHash/cmake_unofficial/libxxhash.a")
    set(KARMA_XXHASH_OBJECT "${CMAKE_BINARY_DIR}/_deps/diligentcore-build/ThirdParty/xxHash/cmake_unofficial/CMakeFiles/xxhash.dir/__/xxhash.c.o")
    set(KARMA_XXHASH_SOURCE "${CMAKE_BINARY_DIR}/_deps/DiligentCore/ThirdParty/xxHash/xxhash.c")
  endif()
endif()

if (KARMA_NETWORK_BACKEND_ENET)
  find_package(enet QUIET)
  if (TARGET enet::enet)
    list(APPEND KARMA_NETWORK_LINK_LIBS enet::enet)
    list(APPEND KARMA_INSTALL_LINK_LIBS enet::enet)
  elseif (TARGET enet_static)
    list(APPEND KARMA_NETWORK_LINK_LIBS enet_static)
    list(APPEND KARMA_INSTALL_LINK_LIBS enet::enet)
  elseif (KARMA_FETCH_DEPS)
    FetchContent_Declare(
      enet
      GIT_REPOSITORY https://github.com/zpl-c/enet.git
      GIT_TAG v2.6.5
    )
    set(ENET_TEST OFF CACHE BOOL "" FORCE)
    set(ENET_SHARED OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(enet)
    if (TARGET enet::enet)
      list(APPEND KARMA_NETWORK_LINK_LIBS enet::enet)
      list(APPEND KARMA_INSTALL_LINK_LIBS enet::enet)
    elseif (TARGET enet_static)
      list(APPEND KARMA_NETWORK_LINK_LIBS enet_static)
      list(APPEND KARMA_INSTALL_LINK_LIBS enet::enet)
    endif()
  endif()
  if (enet_SOURCE_DIR)
    list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${enet_SOURCE_DIR}/include)
  endif()
  if (NOT TARGET enet::enet AND NOT TARGET enet_static)
    message(FATAL_ERROR "ENet backend enabled but ENet was not found. Provide enet or enable KARMA_FETCH_DEPS.")
  endif()
endif()

find_package(assimp QUIET)
if (NOT TARGET assimp::assimp AND KARMA_FETCH_DEPS)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  if (APPLE)
    set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
  endif()
  if (KARMA_ASSIMP_MINIMAL_IMPORTERS)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_STL_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
  endif()
  FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.3.1
  )
  FetchContent_GetProperties(assimp)
  if (NOT assimp_POPULATED)
    FetchContent_Populate(assimp)
  endif()
  add_subdirectory(${assimp_SOURCE_DIR} ${assimp_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
if (TARGET assimp::assimp)
  list(APPEND KARMA_EXTRA_LINK_LIBS assimp::assimp)
  list(APPEND KARMA_INSTALL_LINK_LIBS assimp::assimp)
elseif (TARGET assimp)
  list(APPEND KARMA_EXTRA_LINK_LIBS assimp)
  list(APPEND KARMA_INSTALL_LINK_LIBS assimp::assimp)
else()
  message(FATAL_ERROR "Assimp is required for Karma content import. Provide assimp or enable KARMA_FETCH_DEPS.")
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
    list(APPEND KARMA_INSTALL_LINK_LIBS glfw3::glfw)
  elseif(TARGET glfw3::glfw)
    list(APPEND KARMA_PLATFORM_LINK_LIBS glfw3::glfw)
    list(APPEND KARMA_INSTALL_LINK_LIBS glfw3::glfw)
  else()
    message(FATAL_ERROR "GLFW window backend enabled but GLFW was not found. Provide glfw3 or enable KARMA_FETCH_DEPS.")
  endif()
endif()

if (KARMA_PHYSICS_BACKEND_BULLET)
  find_package(Bullet QUIET)
  if (TARGET Bullet::Bullet)
    list(APPEND KARMA_PHYSICS_LINK_LIBS Bullet::Bullet)
    list(APPEND KARMA_INSTALL_LINK_LIBS Bullet::Bullet)
  elseif (BULLET_LIBRARIES)
    list(APPEND KARMA_PHYSICS_LINK_LIBS ${BULLET_LIBRARIES})
    if (BULLET_INCLUDE_DIRS)
      list(APPEND KARMA_EXTRA_INCLUDE_DIRS ${BULLET_INCLUDE_DIRS})
    endif()
  else()
    message(FATAL_ERROR "Karma: Bullet backend enabled but Bullet was not found.")
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
    list(APPEND KARMA_INSTALL_LINK_LIBS Jolt::Jolt)
  elseif (TARGET Jolt)
    list(APPEND KARMA_PHYSICS_LINK_LIBS Jolt)
    list(APPEND KARMA_INSTALL_LINK_LIBS Jolt::Jolt)
  else()
    message(FATAL_ERROR "Karma: Jolt backend enabled but Jolt was not found. Provide Jolt or enable KARMA_FETCH_DEPS.")
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
    list(APPEND KARMA_INSTALL_LINK_LIBS Recast Detour)
    if (TARGET DebugUtils)
      list(APPEND KARMA_NAVIGATION_LINK_LIBS DebugUtils)
      list(APPEND KARMA_INSTALL_LINK_LIBS DebugUtils)
    endif()
  else()
    message(FATAL_ERROR "Karma: navigation enabled but Recast/Detour targets were not found.")
  endif()
endif()
