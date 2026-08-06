cmake_minimum_required(VERSION 3.16)
project(pacloader LANGUAGES C CXX)

if(NOT (WIN32 OR MINGW OR CMAKE_C_COMPILER MATCHES "mingw"))
    message(FATAL_ERROR "pacloader currently targets 32-bit Windows/MinGW only")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

file(GLOB_RECURSE MINHOOK_SOURCES CONFIGURE_DEPENDS src/minhook/src/*.c)
add_library(minhook STATIC ${MINHOOK_SOURCES})
target_include_directories(minhook PUBLIC src/minhook/include)
target_compile_options(minhook PRIVATE -fPIC -m32)

include(cmake/PacloaderSources.cmake)
add_executable(pacloader ${PACLOADER_SOURCES})
set_target_properties(pacloader PROPERTIES OUTPUT_NAME linuxloader)

target_include_directories(pacloader PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/log
    ${PROJECT_SOURCE_DIR}/src/elfio
    ${PROJECT_SOURCE_DIR}/src/glad/include
    ${PROJECT_SOURCE_DIR}/src/minhook/include
    ${PROJECT_SOURCE_DIR}/src/SDL3)

target_compile_definitions(pacloader PRIVATE
    PACLOADER_BUILD=1
    _WIN32_WINNT=0x0600
    WINVER=0x0600)

target_compile_options(pacloader PRIVATE
    -m32
    -Wall
    -Wextra
    -fpermissive
    -Wno-unused-parameter
    -Wimplicit-fallthrough=0
    -Wstrict-aliasing=0)

target_link_options(pacloader PRIVATE
    -m32
    -static-libgcc
    -static-libstdc++
    "SHELL:-Xlinker --stack -Xlinker 4194304,4194304"
    -Wl,--large-address-aware)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(pacloader PRIVATE -s)
endif()

target_link_directories(pacloader PRIVATE ${PROJECT_SOURCE_DIR}/libs/win32)

# Keep the pthread implementation internal to pacloader.  This leaves SDL3 as
# the only non-system DLL required by the native loader itself.
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=libwinpthread.a
    OUTPUT_VARIABLE PACLOADER_WINPTHREAD
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT IS_ABSOLUTE "${PACLOADER_WINPTHREAD}" OR NOT EXISTS "${PACLOADER_WINPTHREAD}")
    message(FATAL_ERROR "A static 32-bit libwinpthread.a is required")
endif()

target_link_libraries(pacloader PRIVATE
    minhook
    SDL3
    ${PACLOADER_WINPTHREAD}
    gdi32
    winmm
    ole32
    ws2_32
    opengl32
    psapi
    iphlpapi)
