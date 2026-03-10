#include "bha/lsp/server.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/analyzers/all_analyzers.hpp"
#include "bha/parsers/all_parsers.hpp"
#include <iostream>

int main() {
    try {
        bha::build_systems::register_all_adapters();
        bha::analyzers::register_all_analyzers();
        bha::parsers::register_all_parsers();

        bha::lsp::LSPServer server;
        std::cerr << "BHA LSP Server starting..." << std::endl;
        server.run();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
