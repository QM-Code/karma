include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(KARMA_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/karma")

if (KARMA_INSTALL_VCPKG_DEPS)
  if (NOT DEFINED VCPKG_INSTALLED_DIR OR NOT DEFINED VCPKG_TARGET_TRIPLET)
    message(FATAL_ERROR
      "KARMA_INSTALL_VCPKG_DEPS=ON requires configuring with the vcpkg "
      "toolchain so VCPKG_INSTALLED_DIR and VCPKG_TARGET_TRIPLET are defined.")
  endif()

  set(KARMA_VCPKG_TRIPLET_INSTALL_DIR
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  if (NOT IS_DIRECTORY "${KARMA_VCPKG_TRIPLET_INSTALL_DIR}")
    message(FATAL_ERROR
      "KARMA_INSTALL_VCPKG_DEPS=ON could not find the active vcpkg triplet "
      "directory: ${KARMA_VCPKG_TRIPLET_INSTALL_DIR}")
  endif()

  install(DIRECTORY "${KARMA_VCPKG_TRIPLET_INSTALL_DIR}/"
    DESTINATION .
    USE_SOURCE_PERMISSIONS
  )
endif()

install(TARGETS ${KARMA_INSTALL_TARGETS}
  EXPORT karmaTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  ${PROJECT_SOURCE_DIR}/include/karma/app.h
  ${PROJECT_SOURCE_DIR}/include/karma/assets.h
  ${PROJECT_SOURCE_DIR}/include/karma/audio.h
  ${PROJECT_SOURCE_DIR}/include/karma/components.h
  ${PROJECT_SOURCE_DIR}/include/karma/core.h
  ${PROJECT_SOURCE_DIR}/include/karma/foliage.h
  ${PROJECT_SOURCE_DIR}/include/karma/headless.h
  ${PROJECT_SOURCE_DIR}/include/karma/karma.h
  ${PROJECT_SOURCE_DIR}/include/karma/math.h
  ${PROJECT_SOURCE_DIR}/include/karma/navigation.h
  ${PROJECT_SOURCE_DIR}/include/karma/network.h
  ${PROJECT_SOURCE_DIR}/include/karma/physics.h
  ${PROJECT_SOURCE_DIR}/include/karma/platform.h
  ${PROJECT_SOURCE_DIR}/include/karma/prefabs.h
  ${PROJECT_SOURCE_DIR}/include/karma/rendering.h
  ${PROJECT_SOURCE_DIR}/include/karma/scene_authoring.h
  ${PROJECT_SOURCE_DIR}/include/karma/scenes.h
  ${PROJECT_SOURCE_DIR}/include/karma/server.h
  ${PROJECT_SOURCE_DIR}/include/karma/ui.h
  ${PROJECT_SOURCE_DIR}/include/karma/visual.h
  ${PROJECT_SOURCE_DIR}/include/karma/world.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karma
)

install(FILES
  ${KARMA_GENERATED_INCLUDE_DIR}/karma/version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karma
)

if (TARGET karma_imgui_vendor AND imgui_SOURCE_DIR)
  install(FILES
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imconfig.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karma/vendor/imgui
  )
endif()

if (EXISTS "${PROJECT_SOURCE_DIR}/src/rendering/renderer/backends/diligent/shaders")
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/rendering/renderer/backends/diligent/shaders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/karma/shaders/diligent
  )
endif()

install(FILES ${PROJECT_SOURCE_DIR}/LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/karma
)

configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/cmake/karmaConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/karmaConfig.cmake
  INSTALL_DESTINATION ${KARMA_CONFIG_INSTALL_DIR}
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/karmaConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(EXPORT karmaTargets
  FILE karmaTargets.cmake
  NAMESPACE karma::
  DESTINATION ${KARMA_CONFIG_INSTALL_DIR}
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/karmaConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/karmaConfigVersion.cmake
  DESTINATION ${KARMA_CONFIG_INSTALL_DIR}
)
