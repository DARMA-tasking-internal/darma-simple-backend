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

include(cmake/SetDarmaCompilerFlags.cmake)
set_darma_compiler_flags()

include_directories(
  ${DARMA_ROOT}/src/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/src
)

option(ENABLE_BACKEND_DEBUGGING "Whether to turn on debugging features for the backend itself" Off)

option(BOOST_ROOT "Where to find boost (needed for lockfree queue)" "")
option(BACKEND_USE_OPENMP "Whether or not to use OpenMP threads to back the simple backend" Off)
option(BACKEND_USE_KOKKOS "Whether or not to use Kokkos to back the simple backend, enabling Kokkos compatibility" Off)
option(BACKEND_USE_FCONTEXT "Whether or not to use fcontext to back the simple backend with Kokkos." Off)
option(KOKKOS_PATH "Path to Kokkos installation (only used for BACKEND_USE_KOKKOS)" "")
option(FCONTEXT_ROOT "Path to fcontext (boost) installation (only used for BACKEND_USE_FCONTEXT)" "")

if(ENABLE_BACKEND_DEBUGGING)
    add_definitions(-DSIMPLE_BACKEND_DEBUG)
endif()

if (DEFINED BOOST_ROOT)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${BOOST_ROOT}/include")
endif()

if(BACKEND_USE_OPENMP)
    find_package(OpenMP REQUIRED)
    add_definitions(-DSIMPLE_BACKEND_USE_OPENMP)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else()
    if(BACKEND_USE_KOKKOS)
        if (NOT KOKKOS_PATH)
          message(FATAL_ERROR "Need KOKKOS_PATH to be defined")
        endif()
        find_package(OpenMP REQUIRED)
        include(${KOKKOS_PATH}/kokkos.cmake)
        add_definitions(-DSIMPLE_BACKEND_USE_KOKKOS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${KOKKOS_CPPFLAGS} ${KOKKOS_CXXFLAGS}")
        if(BACKEND_USE_FCONTEXT)
            if (NOT FCONTEXT_ROOT)
                message(FATAL_ERROR "Need FCONTEXT_ROOT to be defined")
            endif()
            add_definitions(-DSIMPLE_BACKEND_USE_FCONTEXT)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${FCONTEXT_ROOT}/include")
        endif()
    else()
        find_package( Threads REQUIRED )
    endif()
endif()

add_subdirectory(src)
if(DARMA_SIMPLE_BACKEND_DISABLE_TESTS)
  message(STATUS "Not building simple_backend-specific tests")
else()
  add_subdirectory(tests)
endif()


export ( TARGETS darma_simple_backend
  FILE "${PROJECT_BINARY_DIR}/DarmaSimpleBackendDepends.cmake"
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

if(BACKEND_USE_KOKKOS)
    if(BACKEND_USE_FCONTEXT)
        set_target_properties(darma_simple_backend PROPERTIES 
            LINK_FLAGS "${KOKKOS_LIBS} ${KOKKOS_LDFLAGS} -L${FCONTEXT_ROOT}/lib -lboost_context"
        )
    else()
        set_target_properties(darma_simple_backend PROPERTIES 
            LINK_FLAGS "${KOKKOS_LIBS} ${KOKKOS_LDFLAGS}")
    endif()
endif()
