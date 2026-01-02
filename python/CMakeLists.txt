pybind11_add_module(bha_python
        bha_bindings.cpp
)

set_target_properties(bha_python PROPERTIES
        OUTPUT_NAME "_bha_native"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bha"
)

target_link_libraries(bha_python
        PRIVATE
        bha::bha
        nlohmann_json::nlohmann_json
)

target_include_directories(bha_python
        PRIVATE
        ${CMAKE_SOURCE_DIR}/headers
        ${CMAKE_SOURCE_DIR}/sources
)

install(TARGETS bha_python
        LIBRARY DESTINATION lib/python/bha
)