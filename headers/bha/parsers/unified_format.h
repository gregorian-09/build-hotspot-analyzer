//
// Created by gregorian on 16/10/2025.
//

#ifndef UNIFIED_FORMAT_H
#define UNIFIED_FORMAT_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <string_view>

namespace bha::parsers {

    /**
     * @class UnifiedFormatSerializer
     * Provides serialization and deserialization of BuildTrace and CompilationUnit objects
     *        into a unified, portable JSON-based format.
     *
     * The UnifiedFormatSerializer class is responsible for converting build data structures
     * (such as CompilationUnit and BuildTrace) into a consistent intermediate representation.
     * This enables exporting, importing, and exchanging analysis results across systems
     * and toolchains. It also supports versioning for backward compatibility.
     */
    class UnifiedFormatSerializer {
    public:
        /**
         * Serializes a CompilationUnit object to a unified JSON string.
         *
         * @param unit The compilation unit to serialize.
         * @return A Result containing the JSON string on success, or an error if serialization fails.
         */
        static core::Result<std::string> serialize_compilation_unit(
            const core::CompilationUnit& unit
        );

        /**
         * Serializes a BuildTrace object to a unified JSON string.
         *
         * @param trace The build trace to serialize.
         * @return A Result containing the JSON string or an error on failure.
         */
        static core::Result<std::string> serialize_build_trace(
            const core::BuildTrace& trace
        );

        /**
         * Saves a BuildTrace to a file in unified JSON format.
         *
         * @param trace The build trace to save.
         * @param file_path Path to the output file.
         * @return A Result indicating success or failure.
         */
        static core::Result<void> save_to_file(
            const core::BuildTrace& trace,
            const std::string& file_path
        );

        /**
         * Deserializes a CompilationUnit from a unified JSON string.
         *
         * @param json JSON string representing the serialized compilation unit.
         * @return A Result containing the deserialized CompilationUnit or an error if parsing fails.
         */
        static core::Result<core::CompilationUnit> deserialize_compilation_unit(
            std::string_view json
        );

        /**
         * Deserializes a BuildTrace from a unified JSON string.
         *
         * @param json JSON string representing the serialized build trace.
         * @return A Result containing the deserialized BuildTrace or an error if parsing fails.
         */
        static core::Result<core::BuildTrace> deserialize_build_trace(
            std::string_view json
        );

        /**
         * Loads and deserializes a BuildTrace from a unified JSON file.
         *
         * @param file_path Path to the file to load.
         * @return A Result containing the BuildTrace or an error if the file cannot be read or parsed.
         */
        static core::Result<core::BuildTrace> load_from_file(
            const std::string& file_path
        );

        /**
         * Retrieves the current version of the Unified Intermediate Format (UIF).
         *
         * @return A string representing the UIF version used by the serializer.
         */
        static std::string get_current_version();

    private:
        static constexpr auto UIF_VERSION = "1.0"; ///< Current Unified Intermediate Format version.

        /**
         * Serializes a TemplateInstantiation object to JSON.
         *
         * @param inst The template instantiation to serialize.
         * @return A JSON string representation of the template instantiation.
         */
        static std::string serialize_template_instantiation(
            const core::TemplateInstantiation& inst
        );

        /**
         * Serializes a DependencyGraph into JSON format.
         *
         * @param graph The dependency graph to serialize.
         * @return A JSON string representing the dependency graph.
         */
        static std::string serialize_dependency_graph(
            const core::DependencyGraph& graph
        );

        /**
         * Serializes a MetricsSummary into JSON format.
         *
         * @param metrics The metrics summary to serialize.
         * @return A JSON string representing the metrics summary.
         */
        static std::string serialize_metrics_summary(
            const core::MetricsSummary& metrics
        );

        /**
         * Deserializes a TemplateInstantiation from a JSON string.
         *
         * @param json The JSON representation of the template instantiation.
         * @return A Result containing the deserialized TemplateInstantiation or an error on failure.
         */
        static core::Result<core::TemplateInstantiation> deserialize_template_instantiation(
            const std::string& json
        );
    };
}

#endif //UNIFIED_FORMAT_H
