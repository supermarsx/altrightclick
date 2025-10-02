cmake_minimum_required(VERSION 3.21)

project(altrightclick LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
)

add_executable(altrightclick ${SRC})

target_include_directories(altrightclick PRIVATE include src)

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
