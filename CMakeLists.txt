cmake_minimum_required(VERSION 3.21)

project(altrightclick VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate version header
set(VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${VERSION_HEADER_DIR}/altrightclick")
configure_file(
  src/version.h.in
  ${VERSION_HEADER_DIR}/altrightclick/version.h
  @ONLY
)

# Generate resource file with VERSIONINFO (and optional icon)
set(ARC_ICON "${CMAKE_SOURCE_DIR}/res/altrightclick.ico")
if(EXISTS "${ARC_ICON}")
  set(ARC_ICON_LINE "IDI_APP_ICON ICON \"${ARC_ICON}\"")
else()
  set(ARC_ICON_LINE "")
endif()
configure_file(
  res/altrightclick.rc.in
  ${CMAKE_BINARY_DIR}/altrightclick.rc
  @ONLY
)

# Sources
set(SRC
    src/main.cpp
    src/app.cpp
    src/hook.cpp
    src/config.cpp
    src/tray.cpp
    src/service.cpp
    src/task.cpp
    src/singleton.cpp
    src/log.cpp
)

add_executable(altrightclick ${SRC} ${CMAKE_BINARY_DIR}/altrightclick.rc)

target_include_directories(altrightclick PRIVATE include src ${VERSION_HEADER_DIR})

if(MSVC)
    target_compile_definitions(altrightclick PRIVATE UNICODE _UNICODE)
    target_compile_options(altrightclick PRIVATE /W4 /permissive-)
endif()

# Windows libraries
target_link_libraries(altrightclick PRIVATE user32 shell32 advapi32 ole32)

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
