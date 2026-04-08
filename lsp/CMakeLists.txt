add_library(bha_lsp_core STATIC
    sources/bha/lsp/suggestion_manager.cpp
)
add_library(bha::lsp_core ALIAS bha_lsp_core)

target_include_directories(bha_lsp_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/headers
        ${PROJECT_SOURCE_DIR}/headers
        ${PROJECT_BINARY_DIR}/generated/resources
)

target_link_libraries(bha_lsp_core
    PUBLIC
        bha::bha
    PRIVATE
        bha::warnings
        $<$<BOOL:${BHA_ENABLE_SANITIZERS}>:bha::sanitizers>
        $<$<BOOL:${BHA_ENABLE_COVERAGE}>:bha::coverage>
)

add_executable(bha-lsp
    cli/lsp_server/main.cpp
    sources/bha/lsp/server.cpp
)

target_include_directories(bha-lsp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/headers
        ${PROJECT_SOURCE_DIR}/headers
        ${PROJECT_BINARY_DIR}/generated/resources
)

target_link_libraries(bha-lsp
    PRIVATE
        bha::lsp_core
        bha::warnings
        $<$<BOOL:${BHA_ENABLE_SANITIZERS}>:bha::sanitizers>
        $<$<BOOL:${BHA_ENABLE_COVERAGE}>:bha::coverage>
)
