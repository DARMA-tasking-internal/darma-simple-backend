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
set(sources
    ${sources_GLOB}
    ${data_structures_sources_GLOB}
)
set(headers
    darma_types.h darma_features.h
    ${headers_GLOB}
    ${data_structures_headers_GLOB}
)
add_library(darma_simple_backend SHARED ${sources} ${headers})
set_property(TARGET darma_simple_backend PROPERTY CXX_STANDARD 14)
set_property(TARGET darma_simple_backend PROPERTY CXX_STANDARD_REQUIRED ON)

install(TARGETS darma_simple_backend EXPORT DarmaSimpleBackendDepends DESTINATION lib)
install(FILES ${headers} DESTINATION include)
