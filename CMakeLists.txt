# K Engine — CMake build.
#
# STATUS: WRITTEN BUT NEVER EXECUTED. The development environment that
# produced this file has no cmake binary (see DEVELOPMENT_STATUS.md), so this
# file has not been configured, built or tested by the author. Treat the first
# real `cmake` run (on the project owner's Windows machine) as the verification
# step and record the result in BUILDING.md. The verified build route on Linux
# is the Makefile. MSVC/MinGW support is expected but UNVERIFIED.
#
# Sources are collected by the same globs as the Makefile so the two cannot
# drift on file lists.
cmake_minimum_required(VERSION 3.21)
project(KEngine VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KENGINE_SANITIZE "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)
option(KENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(KENGINE_BUILD_EDITOR_SPIKE "Build the Qt6 + Direct3D 11 viewport spike (Windows only, UNVERIFIED)" OFF)

if(MSVC)
  # Report the real C++ standard in __cplusplus, treat sources as UTF-8, strict conformance.
  add_compile_options(/Zc:__cplusplus /utf-8 /permissive-)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

function(kengine_apply_flags target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(KENGINE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(KENGINE_SANITIZE)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

file(GLOB KENGINE_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Engine/*/*.cpp")
add_library(kengine_core STATIC ${KENGINE_ENGINE_SOURCES})
target_include_directories(kengine_core PUBLIC "${CMAKE_SOURCE_DIR}")
kengine_apply_flags(kengine_core)

add_executable(k "${CMAKE_SOURCE_DIR}/Tools/CLI/main.cpp")
target_link_libraries(k PRIVATE kengine_core)
kengine_apply_flags(k)

file(GLOB KENGINE_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Tests/Unit/*.cpp")
add_executable(k_unit_tests ${KENGINE_UNIT_TEST_SOURCES})
target_link_libraries(k_unit_tests PRIVATE kengine_core)
kengine_apply_flags(k_unit_tests)

enable_testing()
add_test(NAME unit COMMAND k_unit_tests)

option(KENGINE_FULL_TESTS "Also register unit_full (full-size, slow unit tests)" OFF)
if(KENGINE_FULL_TESTS)
  add_test(NAME unit_full COMMAND k_unit_tests)
  set_tests_properties(unit_full PROPERTIES ENVIRONMENT "KENGINE_TEST_FULL=1" TIMEOUT 3600)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME cli
           COMMAND ${CMAKE_COMMAND} -E env "KENGINE_CLI=$<TARGET_FILE:k>"
                   ${Python3_EXECUTABLE} -m unittest discover -s "${CMAKE_SOURCE_DIR}/Tests/CLI" -v)
  add_test(NAME png_probe
           COMMAND ${Python3_EXECUTABLE} -m unittest discover -s "${CMAKE_SOURCE_DIR}/Tests/Visual" -p test_png_probe.py -v)
else()
  message(WARNING "Python 3 not found: the CLI and PNG-probe tests are NOT registered (reported as not-run, not as passed).")
endif()

if(KENGINE_BUILD_EDITOR_SPIKE)
  add_subdirectory(Editor/Spike)
endif()
