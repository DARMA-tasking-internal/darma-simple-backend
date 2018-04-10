FILE(GLOB sources_GLOB "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
FILE(GLOB headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
FILE(GLOB data_structures_sources_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/data_structures/*.cpp"
)
FILE(GLOB data_structures_queue_impl_headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/data_structures/queue_impl/*.hpp"
  )
FILE(GLOB data_structures_headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/data_structures/*.hpp"
)
FILE(GLOB worker_sources_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/worker/*.cpp"
)
FILE(GLOB worker_headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/worker/*.hpp"
)
FILE(GLOB runtime_sources_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp"
)
FILE(GLOB runtime_headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.hpp"
)
FILE(GLOB flow_sources_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/flow/*.cpp"
        )
FILE(GLOB flow_headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/flow/*.hpp"
        )
FILE(GLOB main_sources_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/main/*.cpp"
        )
set(library_sources
    ${sources_GLOB}
    ${data_structures_sources_GLOB}
    ${worker_sources_GLOB}
    ${runtime_sources_GLOB}
    ${flow_sources_GLOB}
)
set(headers
    darma_types.h darma_features.h
    ${headers_GLOB}
    ${data_structures_headers_GLOB}
    ${data_structures_queue_impl_headers_GLOB}
    ${worker_headers_GLOB}
    ${runtime_headers_GLOB}
    ${flow_headers_GLOB}
)

if(BACKEND_USE_KOKKOS)
  message("Not using LibCDS with Kokkos")
  set(SIMPLE_BACKEND_USE_LIBCDS Off)
endif()

if(BACKEND_USE_OPENMP)
  message("Not using LibCDS with OpenMP")
  set(SIMPLE_BACKEND_USE_LIBCDS Off)
endif()

if(BACKEND_USE_FCONTEXT)
  message("Not using LibCDS with fcontext")
  set(SIMPLE_BACKEND_USE_LIBCDS Off)
endif()

add_library(darma_interop SHARED ${library_sources} ${headers})
if(DEFINED SIMPLE_BACKEND_USES_FRONTEND_SOURCE_DIRECTLY)
  target_link_libraries(darma_interop PUBLIC ${CMAKE_THREAD_LIBS_INIT})
else()
  target_link_libraries(darma_interop PUBLIC ${CMAKE_THREAD_LIBS_INIT} darma_frontend)
endif()

if(SIMPLE_BACKEND_USE_LIBCDS)
  FIND_PACKAGE(LibCDS)
  if(LibCDS_FOUND)
    target_link_libraries(darma_interop PUBLIC LibCDS::cds)
    set(DARMA_SIMPLE_BACKEND_HAS_LIBCDS 1)
    message("Found LibCDS at ${LibCDS_DIR}. Simple backend will use LibCDS for some concurrent data structures")
  else()
    message("LibCDS not found")
  endif()
else()
  message("LibCDS explicitly disabled")
endif()

add_library(darma SHARED ${main_sources_GLOB})
target_link_libraries(darma darma_interop)

set_property(TARGET darma_interop PROPERTY CXX_STANDARD 14)
set_property(TARGET darma_interop PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET darma PROPERTY CXX_STANDARD 14)
set_property(TARGET darma PROPERTY CXX_STANDARD_REQUIRED ON)

install(FILES ${headers} DESTINATION include)

install(TARGETS darma EXPORT darma DESTINATION lib)
install(TARGETS darma_interop EXPORT darma_interop DESTINATION lib)

include (CheckCXXSourceCompiles)

check_cxx_source_compiles("
#include <shared_mutex>
int main() {
  std::shared_timed_mutex mtx;
}
"
  DARMA_SIMPLE_BACKEND_HAS_SHARED_MUTEX
)

configure_file(
  config.generated.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.generated.h
)
install (
  FILES ${CMAKE_CURRENT_BINARY_DIR}/config.generated.h
  DESTINATION src
)


