FILE(GLOB sources_GLOB "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
FILE(GLOB headers_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
FILE(GLOB data_structures_sources_GLOB RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/data_structures/*.cpp"
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
set(sources
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
    ${worker_headers_GLOB}
    ${runtime_headers_GLOB}
    ${flow_headers_GLOB}
        data_structures/data_structures_fwd.hpp pending_operation.hpp ready_operation.hpp ready_operation.cpp)
add_library(darma_simple_backend SHARED ${sources} ${headers})
set_property(TARGET darma_simple_backend PROPERTY CXX_STANDARD 14)
set_property(TARGET darma_simple_backend PROPERTY CXX_STANDARD_REQUIRED ON)

install(TARGETS darma_simple_backend EXPORT DarmaSimpleBackendDepends DESTINATION lib)
install(FILES ${headers} DESTINATION include)
