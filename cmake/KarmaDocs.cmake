find_package(Doxygen QUIET)

set(KARMA_DOXYGEN_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(KARMA_DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs/api")
set(KARMA_DOXYGEN_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
  "${KARMA_DOXYGEN_CONFIG}"
  @ONLY
)

if (DOXYGEN_FOUND)
  add_custom_target(karma_docs_api
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${KARMA_DOXYGEN_OUTPUT_DIR}"
    COMMAND "${DOXYGEN_EXECUTABLE}" "${KARMA_DOXYGEN_CONFIG}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating Karma public API documentation"
    VERBATIM
  )
else()
  add_custom_target(karma_docs_api
    COMMAND "${CMAKE_COMMAND}" -E echo
      "Doxygen was not found. Install doxygen, then rebuild target karma_docs_api."
    COMMAND "${CMAKE_COMMAND}" -E false
    VERBATIM
  )
endif()
