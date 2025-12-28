//
// Created by gregorian on 15/12/2025.
//

#include "cli_parser.hpp"
#include "app.hpp"
#include <iostream>
#include <exception>

int main(const int argc, char** argv) {
    try {
        const bha::cli::Options options = bha::cli::CliParser::parse(argc, argv);

        if (options.command == bha::cli::Command::HELP) {
            bha::cli::CliParser::print_help();
            return 0;
        }

        if (options.command == bha::cli::Command::VERSION) {
            bha::cli::CliParser::print_version();
            return 0;
        }

        bha::cli::App app(options);
        return app.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred\n";
        return 1;
    }
}