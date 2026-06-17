include(CMakeFindDependencyMacro)

find_dependency(Threads)
find_dependency(VulkanHeaders CONFIG)
find_dependency(xxHash CONFIG)

get_filename_component(_diligent_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_diligent_include_dir "${_diligent_prefix}/include")
set(_diligent_release_lib_dir "${_diligent_prefix}/lib/DiligentCore/Release")
set(_diligent_debug_lib_dir "${_diligent_prefix}/debug/lib/DiligentCore/Debug")

function(_diligent_add_interface_target target)
  if (NOT TARGET ${target})
    add_library(${target} INTERFACE IMPORTED)
  endif()
  set_target_properties(${target} PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_diligent_include_dir}"
  )
endfunction()

function(_diligent_add_archive_target target release_archive debug_archive)
  if (NOT TARGET ${target})
    add_library(${target} STATIC IMPORTED)
  endif()
  set_target_properties(${target} PROPERTIES
    IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
    IMPORTED_LOCATION_RELEASE "${release_archive}"
    IMPORTED_LOCATION_DEBUG "${debug_archive}"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
    INTERFACE_INCLUDE_DIRECTORIES "${_diligent_include_dir}"
  )
endfunction()

_diligent_add_interface_target(Diligent-PublicBuildSettings)
set_target_properties(Diligent-PublicBuildSettings PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS
    "PLATFORM_LINUX=1;D3D11_SUPPORTED=0;D3D12_SUPPORTED=0;GL_SUPPORTED=0;GLES_SUPPORTED=0;VULKAN_SUPPORTED=1;METAL_SUPPORTED=0;$<$<CONFIG:Debug>:DILIGENT_DEVELOPMENT;DILIGENT_DEBUG>"
)

_diligent_add_interface_target(Diligent-BuildSettings)
set_target_properties(Diligent-BuildSettings PROPERTIES
  INTERFACE_LINK_LIBRARIES Diligent-PublicBuildSettings
  INTERFACE_COMPILE_DEFINITIONS
    "__forceinline=inline;$<$<CONFIG:Debug>:_DEBUG;DEBUG>;$<$<NOT:$<CONFIG:Debug>>:NDEBUG>"
)

foreach(_diligent_interface_target IN ITEMS
    Diligent-PlatformInterface
    Diligent-LinuxPlatform
    Diligent-TargetPlatform
    Diligent-Common
    Diligent-GraphicsEngineInterface
    Diligent-GraphicsEngineVkInterface)
  _diligent_add_interface_target(${_diligent_interface_target})
endforeach()

set_target_properties(Diligent-LinuxPlatform PROPERTIES
  INTERFACE_LINK_LIBRARIES "Diligent-BuildSettings;Diligent-PlatformInterface"
)
set_target_properties(Diligent-TargetPlatform PROPERTIES
  INTERFACE_LINK_LIBRARIES Diligent-LinuxPlatform
)
set_target_properties(Diligent-Common PROPERTIES
  INTERFACE_LINK_LIBRARIES "Diligent-BuildSettings;Diligent-TargetPlatform"
)
set_target_properties(Diligent-GraphicsEngineInterface PROPERTIES
  INTERFACE_LINK_LIBRARIES "Diligent-PublicBuildSettings;Diligent-Common"
)
set_target_properties(Diligent-GraphicsEngineVkInterface PROPERTIES
  INTERFACE_LINK_LIBRARIES "Diligent-GraphicsEngineInterface;Vulkan::Headers"
)

set(_diligent_core_archive_target "")
set(_diligent_extra_archive_targets "")
file(GLOB _diligent_release_archives "${_diligent_release_lib_dir}/*.a")
foreach(_diligent_release_archive IN LISTS _diligent_release_archives)
  get_filename_component(_diligent_archive_name "${_diligent_release_archive}" NAME)
  get_filename_component(_diligent_archive_base "${_diligent_release_archive}" NAME_WE)
  string(REGEX REPLACE "^lib" "" _diligent_archive_base "${_diligent_archive_base}")
  string(REGEX REPLACE "[^A-Za-z0-9_+-]" "_" _diligent_archive_target_suffix "${_diligent_archive_base}")

  set(_diligent_debug_archive "${_diligent_debug_lib_dir}/${_diligent_archive_name}")
  if (NOT EXISTS "${_diligent_debug_archive}")
    set(_diligent_debug_archive "${_diligent_release_archive}")
  endif()

  set(_diligent_archive_target "DiligentCore::archive_${_diligent_archive_target_suffix}")
  _diligent_add_archive_target(
    ${_diligent_archive_target}
    "${_diligent_release_archive}"
    "${_diligent_debug_archive}"
  )

  if (_diligent_archive_base STREQUAL "DiligentCore")
    set(_diligent_core_archive_target ${_diligent_archive_target})
  else()
    list(APPEND _diligent_extra_archive_targets ${_diligent_archive_target})
  endif()
endforeach()

if (NOT _diligent_core_archive_target)
  message(FATAL_ERROR "DiligentCore package is missing libDiligentCore.a in ${_diligent_release_lib_dir}.")
endif()

set(_diligent_archive_link_items ${_diligent_core_archive_target} ${_diligent_extra_archive_targets})
if (UNIX AND NOT APPLE)
  list(PREPEND _diligent_archive_link_items "-Wl,--start-group")
  list(APPEND _diligent_archive_link_items "-Wl,--end-group")
endif()

if (CMAKE_DL_LIBS)
  list(APPEND _diligent_archive_link_items ${CMAKE_DL_LIBS})
endif()
list(APPEND _diligent_archive_link_items Threads::Threads xxHash::xxhash)

_diligent_add_interface_target(Diligent-GraphicsTools)
set_target_properties(Diligent-GraphicsTools PROPERTIES
  INTERFACE_LINK_LIBRARIES "Diligent-BuildSettings;Diligent-Common;Diligent-GraphicsEngineInterface;xxHash::xxhash"
  INTERFACE_COMPILE_DEFINITIONS "DILIGENT_RENDER_STATE_CACHE_SUPPORTED=1"
)

_diligent_add_interface_target(Diligent-GraphicsEngineVk-static)
set_target_properties(Diligent-GraphicsEngineVk-static PROPERTIES
  INTERFACE_LINK_LIBRARIES "${_diligent_archive_link_items};Diligent-BuildSettings;Diligent-GraphicsEngineVkInterface"
)

unset(_diligent_archive_link_items)
unset(_diligent_extra_archive_targets)
unset(_diligent_core_archive_target)
unset(_diligent_release_archives)
unset(_diligent_prefix)
unset(_diligent_include_dir)
unset(_diligent_release_lib_dir)
unset(_diligent_debug_lib_dir)
