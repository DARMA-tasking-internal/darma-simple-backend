# Require a recent version of cmake
cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project (DarmaSimpleBackend CXX)

# module include path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

set (DarmaSimpleBackend_VERSION_MAJOR 0)
set (DarmaSimpleBackend_VERSION_MINOR 5)
set (DarmaSimpleBackend_VERSION_PATCH 1)
set (DarmaSimpleBackend_VERSION
  ${DarmaSimpleBackend_VERSION_MAJOR}.${DarmaSimpleBackend_VERSION_MINOR}.${DarmaSimpleBackend_VERSION_PATCH})

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g")

set (CMAKE_MACOSX_RPATH On)

if (NOT DEFINED DARMA_ROOT)
  set(DARMA_ROOT $ENV{DARMA_ROOT})
endif()

if (NOT DARMA_ROOT)
  message(FATAL_ERROR "Need DARMA_ROOT to be defined")
endif()

include_directories(
  ${DARMA_ROOT}/src/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/src
)

option(ENABLE_BACKEND_DEBUGGING "Whether to turn on debugging features for the backend itself" Off)

if(ENABLE_BACKEND_DEBUGGING)
    add_definitions(-DSIMPLE_BACKEND_DEBUG)
endif()

add_subdirectory(src)
add_subdirectory(tests)

export ( TARGETS darma_simple_backend
  FILE "${PROJECT_BINARY_DIR}/DarmaSimpleDepends.cmake"
)
export ( PACKAGE darma_simple_backend )

configure_file (cmake/DarmaSimpleBackendConfig.cmake.in
  "${PROJECT_BINARY_DIR}/DarmaSimpleBackendConfig.cmake" @ONLY)
configure_file (cmake/DarmaSimpleBackendConfigVersion.cmake.in
  "${PROJECT_BINARY_DIR}/DarmaSimpleBackendConfigVersion.cmake" @ONLY)

# Install DarmaSimpleBackendConfig.cmake and
# DarmaSimpleBackendConfigVersion.cmake
install (FILES
  "${PROJECT_BINARY_DIR}/DarmaSimpleBackendConfig.cmake"
  "${PROJECT_BINARY_DIR}/DarmaSimpleBackendConfigVersion.cmake"
  DESTINATION cmake COMPONENT dev)

install (EXPORT DarmaSimpleBackendDepends DESTINATION cmake COMPONENT dev)
