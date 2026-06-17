vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

set(DILIGENTCORE_REF "9e5b6edcb22b503811e9662c6064a722839b9e1b")
set(DILIGENTCORE_TAG "v2.5.5")
set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/DiligentCore-${DILIGENTCORE_REF}")

vcpkg_find_acquire_program(GIT)

file(REMOVE_RECURSE "${SOURCE_PATH}")
vcpkg_execute_required_process(
  ALLOW_IN_DOWNLOAD_MODE
  COMMAND "${GIT}" clone --depth 1 --branch "${DILIGENTCORE_TAG}" https://github.com/DiligentGraphics/DiligentCore.git "${SOURCE_PATH}"
  WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
  LOGNAME "git-clone-${TARGET_TRIPLET}"
)
vcpkg_execute_required_process(
  ALLOW_IN_DOWNLOAD_MODE
  COMMAND "${GIT}" -C "${SOURCE_PATH}" rev-parse HEAD
  WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
  LOGNAME "git-rev-parse-${TARGET_TRIPLET}"
  OUTPUT_VARIABLE DILIGENTCORE_ACTUAL_REF
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT DILIGENTCORE_ACTUAL_REF STREQUAL DILIGENTCORE_REF)
  message(FATAL_ERROR "DiligentCore ${DILIGENTCORE_TAG} resolved to ${DILIGENTCORE_ACTUAL_REF}, expected ${DILIGENTCORE_REF}.")
endif()
vcpkg_execute_required_process(
  ALLOW_IN_DOWNLOAD_MODE
  COMMAND "${GIT}" -C "${SOURCE_PATH}" submodule update --init --recursive --depth 1
    ThirdParty/glslang
    ThirdParty/SPIRV-Cross
    ThirdParty/SPIRV-Tools
    ThirdParty/SPIRV-Headers
    ThirdParty/volk
  WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
  LOGNAME "git-submodules-${TARGET_TRIPLET}"
)

set(DILIGENTCORE_ROOT_CMAKE "${SOURCE_PATH}/CMakeLists.txt")
file(READ "${DILIGENTCORE_ROOT_CMAKE}" DILIGENTCORE_ROOT_CONTENTS)
string(REPLACE
  "project(DiligentCore)\n"
  "project(DiligentCore)\n\nfind_package(VulkanHeaders CONFIG REQUIRED)\nfind_package(xxHash CONFIG REQUIRED)\n"
  DILIGENTCORE_PATCHED_ROOT_CONTENTS
  "${DILIGENTCORE_ROOT_CONTENTS}"
)
if(DILIGENTCORE_PATCHED_ROOT_CONTENTS STREQUAL DILIGENTCORE_ROOT_CONTENTS)
  message(FATAL_ERROR "Failed to patch DiligentCore root dependency lookup.")
endif()
file(WRITE "${DILIGENTCORE_ROOT_CMAKE}" "${DILIGENTCORE_PATCHED_ROOT_CONTENTS}")

set(DILIGENTCORE_THIRD_PARTY_CMAKE "${SOURCE_PATH}/ThirdParty/CMakeLists.txt")
file(READ "${DILIGENTCORE_THIRD_PARTY_CMAKE}" DILIGENTCORE_THIRD_PARTY_CONTENTS)

set(DILIGENTCORE_PATCHED_CONTENTS "${DILIGENTCORE_THIRD_PARTY_CONTENTS}")
string(REPLACE
  "if (VULKAN_SUPPORTED)\n    if (NOT TARGET Vulkan::Headers)"
  "if (VULKAN_SUPPORTED)\n    if (NOT TARGET Vulkan::Headers)\n        find_package(VulkanHeaders CONFIG QUIET)\n    endif()\n    if (NOT TARGET Vulkan::Headers)"
  DILIGENTCORE_PATCHED_CONTENTS
  "${DILIGENTCORE_PATCHED_CONTENTS}"
)
string(REPLACE
  "if (NOT TARGET xxHash::xxhash)\n    option(BUILD_SHARED_LIBS"
  "if (NOT TARGET xxHash::xxhash)\n    find_package(xxHash CONFIG QUIET)\nendif()\n\nif (NOT TARGET xxHash::xxhash)\n    option(BUILD_SHARED_LIBS"
  DILIGENTCORE_PATCHED_CONTENTS
  "${DILIGENTCORE_PATCHED_CONTENTS}"
)
if(DILIGENTCORE_PATCHED_CONTENTS STREQUAL DILIGENTCORE_THIRD_PARTY_CONTENTS)
  message(FATAL_ERROR "Failed to patch DiligentCore third-party dependency lookup.")
endif()
file(WRITE "${DILIGENTCORE_THIRD_PARTY_CMAKE}" "${DILIGENTCORE_PATCHED_CONTENTS}")

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DDILIGENT_NO_DIRECT3D11=ON
    -DDILIGENT_NO_DIRECT3D12=ON
    -DDILIGENT_NO_VULKAN=OFF
    -DDILIGENT_NO_OPENGL=ON
    -DDILIGENT_NO_METAL=ON
    -DDILIGENT_NO_ARCHIVER=OFF
    -DDILIGENT_BUILD_TESTS=OFF
    -DDILIGENT_NO_FORMAT_VALIDATION=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()

foreach(DILIGENTCORE_CONFIG IN ITEMS Release Debug)
  if(DILIGENTCORE_CONFIG STREQUAL "Debug")
    set(DILIGENTCORE_PACKAGE_LIB_ROOT "${CURRENT_PACKAGES_DIR}/debug/lib")
  else()
    set(DILIGENTCORE_PACKAGE_LIB_ROOT "${CURRENT_PACKAGES_DIR}/lib")
  endif()

  if(EXISTS "${DILIGENTCORE_PACKAGE_LIB_ROOT}/${DILIGENTCORE_CONFIG}")
    file(MAKE_DIRECTORY "${DILIGENTCORE_PACKAGE_LIB_ROOT}/DiligentCore")
    file(RENAME
      "${DILIGENTCORE_PACKAGE_LIB_ROOT}/${DILIGENTCORE_CONFIG}"
      "${DILIGENTCORE_PACKAGE_LIB_ROOT}/DiligentCore/${DILIGENTCORE_CONFIG}"
    )
  endif()
endforeach()

file(GLOB_RECURSE DILIGENTCORE_SHARED_LIBS
  "${CURRENT_PACKAGES_DIR}/lib/DiligentCore/*.so"
  "${CURRENT_PACKAGES_DIR}/lib/DiligentCore/*.so.*"
  "${CURRENT_PACKAGES_DIR}/debug/lib/DiligentCore/*.so"
  "${CURRENT_PACKAGES_DIR}/debug/lib/DiligentCore/*.so.*"
)
if(DILIGENTCORE_SHARED_LIBS)
  file(REMOVE ${DILIGENTCORE_SHARED_LIBS})
endif()

file(REMOVE_RECURSE
  "${CURRENT_PACKAGES_DIR}/debug/include"
  "${CURRENT_PACKAGES_DIR}/debug/share"
  "${CURRENT_PACKAGES_DIR}/Licenses"
  "${CURRENT_PACKAGES_DIR}/debug/Licenses"
)

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/${PORT}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/DiligentCoreConfig.cmake" "${CURRENT_PACKAGES_DIR}/share/${PORT}/DiligentCoreConfig.cmake" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)

vcpkg_install_copyright(FILE_LIST
  "${SOURCE_PATH}/License.txt"
  "${SOURCE_PATH}/ThirdParty/SPIRV-Cross/LICENSE"
  "${SOURCE_PATH}/ThirdParty/SPIRV-Headers/LICENSE"
  "${SOURCE_PATH}/ThirdParty/SPIRV-Tools/LICENSE"
  "${SOURCE_PATH}/ThirdParty/glslang/LICENSE.txt"
  "${SOURCE_PATH}/ThirdParty/volk/LICENSE.md"
  "${SOURCE_PATH}/ThirdParty/DirectXShaderCompiler/LICENSE.TXT"
  "${SOURCE_PATH}/ThirdParty/DirectXShaderCompiler/ThirdPartyNotices.txt"
)
