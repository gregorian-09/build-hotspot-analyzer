add_executable(bha-lsp
    cli/lsp_server/main.cpp
    sources/bha/lsp/server.cpp
    sources/bha/lsp/suggestion_manager.cpp
)

target_include_directories(bha-lsp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/headers
        ${PROJECT_SOURCE_DIR}/headers
        ${PROJECT_BINARY_DIR}/generated/resources
)

target_link_libraries(bha-lsp
    PRIVATE
        bha::bha
        bha::warnings
        $<$<BOOL:${BHA_ENABLE_SANITIZERS}>:bha::sanitizers>
        $<$<BOOL:${BHA_ENABLE_COVERAGE}>:bha::coverage>
)
