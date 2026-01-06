//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/cli/commands/command.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace bha::cli
{
    // ============================================================================
    // ParsedArgs Implementation
    // ============================================================================

    void ParsedArgs::set(const std::string& name, const std::string& value) {
        args_[name] = value;
    }

    void ParsedArgs::set_flag(const std::string& name) {
        flags_[name] = true;
    }

    void ParsedArgs::add_positional(const std::string& value) {
        positional_.push_back(value);
    }

    bool ParsedArgs::has(const std::string& name) const {
        return args_.contains(name) || flags_.contains(name);
    }

    std::optional<std::string> ParsedArgs::get(const std::string& name) const {
        if (const auto it = args_.find(name); it != args_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string ParsedArgs::get_or(const std::string& name, const std::string& default_val) const {
        const auto val = get(name);
        return val.value_or(default_val);
    }

    std::optional<int> ParsedArgs::get_int(const std::string& name) const {
        const auto val = get(name);
        if (!val) return std::nullopt;
        try {
            return std::stoi(*val);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<double> ParsedArgs::get_double(const std::string& name) const {
        const auto val = get(name);
        if (!val) return std::nullopt;
        try {
            return std::stod(*val);
        } catch (...) {
            return std::nullopt;
        }
    }

    bool ParsedArgs::get_flag(const std::string& name) const {
        const auto it = flags_.find(name);
        return it != flags_.end() && it->second;
    }

    // ============================================================================
    // Command Implementation
    // ============================================================================

    std::string Command::usage() const {
        std::ostringstream ss;
        ss << "Usage: bha " << name();

        for (const auto args = arguments(); const auto& arg : args) {
            if (arg.required) {
                ss << " --" << arg.name << " <" << arg.value_name << ">";
            }
        }

        ss << " [OPTIONS]";
        return ss.str();
    }

    std::string Command::validate(const ParsedArgs& args) const {
        for (const auto& def : arguments()) {
            if (def.required && !args.has(def.name)) {
                return "Missing required argument: --" + def.name;
            }
        }
        return "";
    }

    void Command::print_help() const {
        std::cout << description() << "\n\n";
        std::cout << usage() << "\n\n";

        if (const auto args = arguments(); !args.empty()) {
            std::cout << "Options:\n";
            for (const auto& arg : args) {
                std::cout << "  ";
                if (arg.short_name) {
                    std::cout << "-" << arg.short_name << ", ";
                } else {
                    std::cout << "    ";
                }
                std::cout << "--" << std::left << std::setw(20) << arg.name;
                std::cout << arg.description;
                if (!arg.default_value.empty()) {
                    std::cout << " (default: " << arg.default_value << ")";
                }
                if (arg.required) {
                    std::cout << " [required]";
                }
                std::cout << "\n";
            }
        }

        std::cout << "\n";
        std::cout << "Common options:\n";
        std::cout << "  -h, --help                Show this help message\n";
        std::cout << "  -v, --verbose             Enable verbose output\n";
        std::cout << "  -q, --quiet               Only show errors\n";
        std::cout << "  --json                    Output in JSON format\n";
    }

    void Command::print(const std::string_view msg) const {
        if (verbosity_ != Verbosity::Quiet) {
            std::cout << msg << "\n";
        }
    }

    void Command::print_error(const std::string_view msg)
    {
        std::cerr << "error: " << msg << "\n";
    }

    void Command::print_warning(const std::string_view msg) const {
        if (verbosity_ != Verbosity::Quiet) {
            std::cerr << "warning: " << msg << "\n";
        }
    }

    void Command::print_verbose(const std::string_view msg) const {
        if (verbosity_ >= Verbosity::Verbose) {
            std::cout << msg << "\n";
        }
    }

    void Command::print_debug(const std::string_view msg) const {
        if (verbosity_ >= Verbosity::Debug) {
            std::cout << "[DEBUG] " << msg << "\n";
        }
    }

    // ============================================================================
    // CommandRegistry Implementation
    // ============================================================================

    CommandRegistry& CommandRegistry::instance() {
        static CommandRegistry instance;
        return instance;
    }

    void CommandRegistry::register_command(std::unique_ptr<Command> cmd) {
        commands_.push_back(std::move(cmd));
    }

    Command* CommandRegistry::find(const std::string_view name) const {
        for (const auto& cmd : commands_) {
            if (cmd->name() == name) {
                return cmd.get();
            }
        }
        return nullptr;
    }

    std::vector<Command*> CommandRegistry::list() const {
        std::vector<Command*> result;
        result.reserve(commands_.size());
        for (const auto& cmd : commands_) {
            result.push_back(cmd.get());
        }
        return result;
    }

    // ============================================================================
    // Argument Parser
    // ============================================================================

    ParseResult parse_arguments(
        const std::vector<std::string>& args,
        const std::vector<ArgDef>& defs
    ) {
        ParseResult result;
        result.success = true;

        // Build lookup maps
        std::unordered_map<std::string, const ArgDef*> long_map;
        std::unordered_map<char, const ArgDef*> short_map;

        for (const auto& def : defs) {
            long_map[def.name] = &def;
            if (def.short_name) {
                short_map[def.short_name] = &def;
            }
            if (!def.default_value.empty()) {
                result.args.set(def.name, def.default_value);
            }
        }

        bool options_ended = false;  // Set to true after seeing "--"

        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];

            if (arg.empty()) continue;

            if (arg == "--" && !options_ended) {
                options_ended = true;
                continue;
            }

            if (arg[0] == '-' && !options_ended) {
                if (arg.size() > 1 && arg[1] == '-') {
                    // Long option
                    std::string name = arg.substr(2);
                    std::string value;

                    // Check for --name=value format
                    if (auto eq_pos = name.find('='); eq_pos != std::string::npos) {
                        value = name.substr(eq_pos + 1);
                        name = name.substr(0, eq_pos);
                    }

                    auto it = long_map.find(name);
                    if (it == long_map.end()) {
                        if (name == "help" || name == "verbose" || name == "quiet" || name == "json") {
                            result.args.set_flag(name);
                            continue;
                        }
                        result.error = "Unknown option: --" + name;
                        result.success = false;
                        return result;
                    }

                    if (const ArgDef* def = it->second; def->takes_value) {
                        if (value.empty() && i + 1 < args.size()) {
                            value = args[++i];
                        }
                        if (value.empty()) {
                            result.error = "Option --" + name + " requires a value";
                            result.success = false;
                            return result;
                        }
                        result.args.set(name, value);
                    } else {
                        result.args.set_flag(name);
                    }
                } else {
                    // Short option(s)
                    for (std::size_t j = 1; j < arg.size(); ++j) {
                        char c = arg[j];

                        // Common options
                        if (c == 'h') {
                            result.args.set_flag("help");
                            continue;
                        }
                        if (c == 'v') {
                            result.args.set_flag("verbose");
                            continue;
                        }
                        if (c == 'q') {
                            result.args.set_flag("quiet");
                            continue;
                        }

                        auto it = short_map.find(c);
                        if (it == short_map.end()) {
                            result.error = std::string("Unknown option: -") + c;
                            result.success = false;
                            return result;
                        }

                        if (const ArgDef* def = it->second; def->takes_value) {
                            std::string value;
                            if (j + 1 < arg.size()) {
                                value = arg.substr(j + 1);
                            } else if (i + 1 < args.size()) {
                                value = args[++i];
                            }
                            if (value.empty()) {
                                result.error = std::string("Option -") + c + " requires a value";
                                result.success = false;
                                return result;
                            }
                            result.args.set(def->name, value);
                            break;  // Rest of short options consumed as value
                        } else {
                            result.args.set_flag(def->name);
                        }
                    }
                }
            } else {
                result.args.add_positional(arg);
            }
        }

        return result;
    }
}  // namespace bha::cli