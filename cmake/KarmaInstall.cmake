include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(KARMA_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/karma")

install(TARGETS karma
  EXPORT karmaTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PATTERN "AGENTS.md" EXCLUDE
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
