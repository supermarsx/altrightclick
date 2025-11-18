cmake_minimum_required(VERSION 3.21)

project(altrightclick VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckIncludeFileCXX)
check_include_file_cxx("windows.h" HAVE_WINDOWS_H)
check_include_file_cxx("filesystem" HAVE_FILESYSTEM)

# Generate version header
set(VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${VERSION_HEADER_DIR}/altrightclick")
configure_file(
  src/version.h.in
  ${VERSION_HEADER_DIR}/altrightclick/version.h
  @ONLY
)

# Generate resource file with VERSIONINFO (and icon generated at build)
# We'll generate the .ico into the build directory so it can be produced during build
set(ARC_ICON "${CMAKE_BINARY_DIR}/altrightclick_multi.ico")
set(ARC_ICON_LINE "IDI_APP_ICON ICON \"${ARC_ICON}\"")
configure_file(
  res/altrightclick.rc.in
  ${CMAKE_BINARY_DIR}/altrightclick.rc
  @ONLY
)

# Small programmatic icon generator
add_executable(icon_gen src/icon_gen.cpp)
if (MSVC)
  target_compile_definitions(icon_gen PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(icon_gen PRIVATE /W4 /permissive-)
  target_link_libraries(icon_gen PRIVATE gdiplus)
endif()

# Custom command: run the icon_gen executable to create the ico in the build dir
add_custom_command(
  OUTPUT ${ARC_ICON}
  COMMAND $<TARGET_FILE:icon_gen> ${ARC_ICON}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS icon_gen
  COMMENT "Generating programmatic icon ${ARC_ICON}"
)

add_custom_target(generate_icon DEPENDS ${ARC_ICON})

# Sources
set(SRC
    src/main.cpp
    src/app.cpp
    src/hook.cpp
    src/config.cpp
    src/persistence.cpp
    src/tray.cpp
    src/service.cpp
    src/task.cpp
    src/singleton.cpp
    src/log.cpp
)

add_executable(altrightclick ${SRC} ${CMAKE_BINARY_DIR}/altrightclick.rc)
add_dependencies(altrightclick generate_icon)

target_include_directories(altrightclick PRIVATE include src ${VERSION_HEADER_DIR})

if(MSVC)
    target_compile_definitions(altrightclick PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(altrightclick PRIVATE /W4 /permissive-)
endif()

# Windows libraries
target_link_libraries(altrightclick PRIVATE user32 shell32 advapi32 ole32)

# Windows toolchain required
if(NOT HAVE_WINDOWS_H)
  message(FATAL_ERROR "windows.h not found; please configure with a Windows toolchain.")
endif()

# Set subsystem to console for CLI/service management
if (MSVC)
    # Keep console for debugging and CLI control
    # To build as GUI subsystem, uncomment below line
    # target_link_options(altrightclick PRIVATE "/SUBSYSTEM:WINDOWS")
endif()

install(TARGETS altrightclick RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Platform: ${CMAKE_GENERATOR_PLATFORM}")

# -----------------------------
# Tests
# -----------------------------
include(CTest)
if (BUILD_TESTING)
  add_executable(config_test tests/config_test.cpp)
  target_sources(config_test PRIVATE src/config.cpp src/log.cpp)
  target_include_directories(config_test PRIVATE include src ${VERSION_HEADER_DIR})
  if (MSVC)
    target_compile_definitions(config_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(config_test PRIVATE /W4 /permissive-)
  endif()
  target_link_libraries(config_test PRIVATE user32 shell32 advapi32 ole32)
  add_test(NAME config_test COMMAND config_test)

  add_executable(config_edge_test tests/config_edge_test.cpp)
  target_sources(config_edge_test PRIVATE src/config.cpp src/log.cpp)
  target_include_directories(config_edge_test PRIVATE include src ${VERSION_HEADER_DIR})
  if (MSVC)
    target_compile_definitions(config_edge_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(config_edge_test PRIVATE /W4 /permissive-)
  endif()
  target_link_libraries(config_edge_test PRIVATE user32 shell32 advapi32 ole32)
  add_test(NAME config_edge_test COMMAND config_edge_test)

  # Icon validation test - ensure generated ICO contains expected sizes
  add_executable(icon_test tests/icon_test.cpp)
  add_dependencies(icon_test generate_icon)
  target_include_directories(icon_test PRIVATE include ${VERSION_HEADER_DIR})
  if (MSVC)
    target_compile_definitions(icon_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(icon_test PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME icon_test COMMAND icon_test ${CMAKE_BINARY_DIR}/altrightclick_multi.ico)
endif()

# -----------------------------
# Doxygen docs (optional)
# -----------------------------
find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
  # Allow version substitution in Doxyfile via CMake configure
  set(ALTRIGHTCLICK_VERSION ${PROJECT_VERSION})
  # We keep a plain Doxyfile that uses @ALTRIGHTCLICK_VERSION@ token; replace it here.
  file(READ ${CMAKE_SOURCE_DIR}/docs/Doxyfile DOXY_IN)
  string(REPLACE "@ALTRIGHTCLICK_VERSION@" "${ALTRIGHTCLICK_VERSION}" DOXY_CFG "${DOXY_IN}")
  file(WRITE ${CMAKE_BINARY_DIR}/Doxyfile "${DOXY_CFG}")
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
else()
  message(STATUS "Doxygen not found; 'docs' target will be unavailable.")
endif()
