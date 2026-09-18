#include "bha/lsp/server.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/analyzers/all_analyzers.hpp"
#include "bha/parsers/all_parsers.hpp"
#include <cstdio>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main() {
    try {
#ifdef _WIN32
        // LSP framing is byte-counted; CRT text mode rewrites CRLF on the wire.
        if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
            _setmode(_fileno(stdout), _O_BINARY) == -1) {
            std::cerr << "Failed to configure binary LSP transport" << std::endl;
            return 1;
        }
#endif
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
