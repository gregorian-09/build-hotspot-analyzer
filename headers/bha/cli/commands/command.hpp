//
// Created by gregorian-rayne on 1/2/26.
//

#ifndef BHA_COMMAND_HPP
#define BHA_COMMAND_HPP

/**
 * @file command.hpp
 * @brief Base class for CLI commands.
 *
 * Provides a common interface for all CLI commands with support for:
 * - Argument parsing
 * - Help text generation
 * - Progress reporting
 * - Output formatting
 */

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>
#include <unordered_map>

namespace bha::cli
{
    /**
     * Command-line argument definition.
     */
    struct ArgDef {
        std::string name;           // Long name (--name)
        char short_name = 0;        // Short name (-n)
        std::string description;
        bool required = false;
        bool takes_value = true;    // false for flags
        std::string default_value;
        std::string value_name = "VALUE";  // For help text
    };

    /**
     * Parsed command-line arguments.
     */
    class ParsedArgs {
    public:
        void set(const std::string& name, const std::string& value);
        void set_flag(const std::string& name);
        void add_positional(const std::string& value);

        [[nodiscard]] bool has(const std::string& name) const;
        [[nodiscard]] std::optional<std::string> get(const std::string& name) const;
        [[nodiscard]] std::string get_or(const std::string& name, const std::string& default_val) const;
        [[nodiscard]] std::optional<int> get_int(const std::string& name) const;
        [[nodiscard]] std::optional<double> get_double(const std::string& name) const;
        [[nodiscard]] bool get_flag(const std::string& name) const;
        [[nodiscard]] const std::vector<std::string>& positional() const { return positional_; }

    private:
        std::unordered_map<std::string, std::string> args_;
        std::unordered_map<std::string, bool> flags_;
        std::vector<std::string> positional_;
    };

    /**
     * Output verbosity level.
     */
    enum class Verbosity {
        Quiet,      // Only errors
        Normal,     // Standard output
        Verbose,    // Extra details
        Debug       // All information
    };

    /**
     * Output format for results.
     */
    enum class OutputFormat {
        Text,       // Human-readable text
        JSON,       // Machine-readable JSON
        Table       // Tabular format
    };

    /**
     * Base class for all CLI commands.
     */
    class Command {
    public:
        virtual ~Command() = default;

        /**
         * Returns the command name (e.g., "analyze").
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * Returns a short description for help text.
         */
        [[nodiscard]] virtual std::string_view description() const noexcept = 0;

        /**
         * Returns detailed usage examples.
         */
        [[nodiscard]] virtual std::string usage() const;

        /**
         * Returns argument definitions for this command.
         */
        [[nodiscard]] virtual std::vector<ArgDef> arguments() const { return {}; }

        /**
         * Executes the command.
         *
         * @param args Parsed command-line arguments.
         * @return Exit code (0 = success).
         */
        [[nodiscard]] virtual int execute(const ParsedArgs& args) = 0;

        /**
         * Validates arguments before execution.
         *
         * @param args Parsed arguments.
         * @return Error message if invalid, empty if valid.
         */
        [[nodiscard]] virtual std::string validate(const ParsedArgs& args) const;

        /**
         * Prints help for this command.
         */
        void print_help() const;

    protected:
        // Output helpers
        void set_verbosity(Verbosity v) { verbosity_ = v; }
        void set_output_format(OutputFormat f) { output_format_ = f; }

        void print(std::string_view msg) const;
        static void print_error(std::string_view msg);
        void print_warning(std::string_view msg) const;
        void print_verbose(std::string_view msg) const;
        void print_debug(std::string_view msg) const;

        [[nodiscard]] Verbosity verbosity() const { return verbosity_; }
        [[nodiscard]] OutputFormat output_format() const { return output_format_; }
        [[nodiscard]] bool is_quiet() const { return verbosity_ == Verbosity::Quiet; }
        [[nodiscard]] bool is_verbose() const { return verbosity_ >= Verbosity::Verbose; }
        [[nodiscard]] bool is_json() const { return output_format_ == OutputFormat::JSON; }

    private:
        Verbosity verbosity_ = Verbosity::Normal;
        OutputFormat output_format_ = OutputFormat::Text;
    };

    /**
     * Registry for managing CLI commands.
     */
    class CommandRegistry {
    public:
        static CommandRegistry& instance();

        void register_command(std::unique_ptr<Command> cmd);

        [[nodiscard]] Command* find(std::string_view name) const;
        [[nodiscard]] std::vector<Command*> list() const;

    private:
        CommandRegistry() = default;
        std::vector<std::unique_ptr<Command>> commands_;
    };

    /**
     * Parses command-line arguments for a command.
     *
     * @param args Command-line arguments (after command name).
     * @param defs Argument definitions.
     * @return Parsed arguments or error message.
     */
    struct ParseResult {
        ParsedArgs args;
        std::string error;
        bool success = true;
    };

    [[nodiscard]] ParseResult parse_arguments(
        const std::vector<std::string>& args,
        const std::vector<ArgDef>& defs
    );
}  // namespace bha::cli

#endif //BHA_COMMAND_HPP