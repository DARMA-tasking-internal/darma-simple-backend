# Require a recent version of cmake
cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project (DarmaSimpleBackend CXX)

option(darma_frontend_DIR "Root for DARMA frontend installation" OFF)
if (NOT darma_frontend_DIR)
  message(FATAL_ERROR "Must specify valid DARMA frontend installation root with -Ddarma_frontend_DIR=")
endif()

find_package(darma_frontend PATHS ${darma_frontend_DIR}/cmake)

if(NOT darma_frontend_FOUND)
  message(FATAL_ERROR "Directory ${darma_frontend_DIR} does not exist or does not contain valid DARMA frontend install")
endif()

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

include(cmake/SetDarmaCompilerFlags.cmake)


option(ENABLE_BACKEND_DEBUGGING "Whether to turn on debugging features for the backend itself" Off)

option(BOOST_ROOT "Where to find boost (needed for lockfree queue)" "")
option(BACKEND_USE_OPENMP "Whether or not to use OpenMP threads to back the simple backend" Off)
option(BACKEND_USE_KOKKOS "Whether or not to use Kokkos to back the simple backend, enabling Kokkos compatibility" Off)
option(BACKEND_USE_FCONTEXT "Whether or not to use fcontext to back the simple backend with Kokkos." Off)
option(KOKKOS_PATH "Path to Kokkos installation (only used for BACKEND_USE_KOKKOS)" "")
option(FCONTEXT_ROOT "Path to fcontext (boost) installation (only used for BACKEND_USE_FCONTEXT)" "")
option(SIMPLE_BACKEND_USE_LIBCDS "Whether or not to use LibCDS, if available, for concurrent data structures" OFF)

add_subdirectory(src)
if(DARMA_SIMPLE_BACKEND_DISABLE_TESTS)
  message(STATUS "Not building simple_backend-specific tests")
else()
  add_subdirectory(tests)
endif()


if(ENABLE_BACKEND_DEBUGGING)
  target_compile_definitions(darma_interop PUBLIC -DSIMPLE_BACKEND_DEBUG)
endif()

if (BOOST_ROOT)
  target_include_directories(darma_interop ${BOOST_ROOT}/include)
  target_compile_definitions(darma_interop PUBLIC -DSIMPLE_BACKEND_HAS_BOOST)
endif()

target_include_directories(darma_interop PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>  # <prefix>/include/
)

target_include_directories(darma_interop PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>
  $<INSTALL_INTERFACE:include>  # <prefix>/include/
)

set_darma_compiler_flags()
target_compile_options(darma_interop PUBLIC ${DARMA_REQUIRED_CXX_FLAGS})

install(
  EXPORT darma
  DESTINATION cmake 
)
install(
  EXPORT darma_interop
  DESTINATION cmake 
)

configure_file(
  cmake/darmaConfig.cmake.in
  "${PROJECT_BINARY_DIR}/darmaConfig.cmake" @ONLY
)

install(
  FILES
  "${PROJECT_BINARY_DIR}/darmaConfig.cmake" 
  COMPONENT dev
  DESTINATION cmake
)

