# Require a recent version of cmake
cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project (DarmaSimpleCVBackend CXX)

# module include path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

set (DarmaSimpleCVBackend_VERSION_MAJOR 0)
set (DarmaSimpleCVBackend_VERSION_MINOR 5)
set (DarmaSimpleCVBackend_VERSION_PATCH 1)
set (DarmaSimpleCVBackend_VERSION
  ${DarmaSimpleCVBackend_VERSION_MAJOR}.${DarmaSimpleCVBackend_VERSION_MINOR}.${DarmaSimpleCVBackend_VERSION_PATCH})

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
add_subdirectory(examples)

export ( TARGETS darma_SimpleCV_backend
  FILE "${PROJECT_BINARY_DIR}/DarmaSimpleCVDepends.cmake"
)
export ( PACKAGE darma_SimpleCV_backend )

configure_file (cmake/DarmaSimpleCVBackendConfig.cmake.in
  "${PROJECT_BINARY_DIR}/DarmaSimpleCVBackendConfig.cmake" @ONLY)
configure_file (cmake/DarmaSimpleCVBackendConfigVersion.cmake.in
  "${PROJECT_BINARY_DIR}/DarmaSimpleCVBackendConfigVersion.cmake" @ONLY)

# Install DarmaSimpleCVBackendConfig.cmake and
# DarmaSimpleCVBackendConfigVersion.cmake
install (FILES
  "${PROJECT_BINARY_DIR}/DarmaSimpleCVBackendConfig.cmake"
  "${PROJECT_BINARY_DIR}/DarmaSimpleCVBackendConfigVersion.cmake"
  DESTINATION cmake COMPONENT dev)

install (EXPORT DarmaSimpleCVBackendDepends DESTINATION cmake COMPONENT dev)
