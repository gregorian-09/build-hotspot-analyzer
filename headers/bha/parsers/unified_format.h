//
// Created by gregorian on 16/10/2025.
//

#ifndef UNIFIED_FORMAT_H
#define UNIFIED_FORMAT_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <string_view>
#include <vector>

namespace bha::parsers {

    /**
     * @class UnifiedFormatSerializer
     * Provides serialization and deserialization of BuildTrace and CompilationUnit objects
     * into a unified, portable JSON-based format.
     *
     * Converts build data structures into a consistent intermediate representation.
     * Supports versioning and full round-trip serialization/deserialization.
     */
    class UnifiedFormatSerializer {
    public:
        /**
         * Serializes a CompilationUnit into a JSON string.
         *
         * @param unit The CompilationUnit to serialize.
         * @return A Result containing the serialized JSON string on success, or an error.
         */
        static core::Result<std::string> serialize_compilation_unit(const core::CompilationUnit& unit);

        /**
         * Serializes a BuildTrace into a JSON string.
         *
         * @param trace The BuildTrace to serialize.
         * @return A Result containing the serialized JSON string on success, or an error.
         */
        static core::Result<std::string> serialize_build_trace(const core::BuildTrace& trace);

        /**
         * Saves a BuildTrace to a file in JSON format.
         *
         * @param trace The BuildTrace to save.
         * @param file_path The file path where the JSON will be written.
         * @return A Result indicating success or failure.
         */
        static core::Result<void> save_to_file(const core::BuildTrace& trace, const std::string& file_path);

        /**
         * Deserializes a CompilationUnit from a JSON string.
         *
         * @param json JSON string representing the CompilationUnit.
         * @return A Result containing the deserialized CompilationUnit or an error.
         */
        static core::Result<core::CompilationUnit> deserialize_compilation_unit(std::string_view json);

        /**
         * Deserializes a BuildTrace from a JSON string.
         *
         * @param json JSON string representing the BuildTrace.
         * @return A Result containing the deserialized BuildTrace or an error.
         */
        static core::Result<core::BuildTrace> deserialize_build_trace(std::string_view json);

        /**
         * Loads and deserializes a BuildTrace from a JSON file.
         *
         * @param file_path Path to the JSON file.
         * @return A Result containing the deserialized BuildTrace or an error.
         */
        static core::Result<core::BuildTrace> load_from_file(const std::string& file_path);

        /**
         * Retrieves the current version of the Unified Intermediate Format.
         *
         * @return The current UIF version string.
         */
        static std::string get_current_version();

    private:
        static constexpr auto UIF_VERSION = "1.0";

        /**
         * Serializes a TemplateInstantiation object to a JSON string.
         *
         * @param inst The TemplateInstantiation object.
         * @return The JSON string representing the object.
         */
        static std::string serialize_template_instantiation(const core::TemplateInstantiation& inst);

        /**
         * Deserializes a TemplateInstantiation from a JSON string.
         *
         * @param json JSON string representing the TemplateInstantiation.
         * @return A Result containing the deserialized object or an error.
         */
        static core::Result<core::TemplateInstantiation> deserialize_template_instantiation(const std::string& json);

        /**
         * Serializes a DependencyGraph to a JSON string.
         *
         * @param graph The DependencyGraph to serialize.
         * @return The JSON string representing the graph.
         */
        static std::string serialize_dependency_graph(const core::DependencyGraph& graph);

        /**
         * Deserializes a DependencyGraph from a JSON string.
         *
         * @param json JSON string representing the DependencyGraph.
         * @return The deserialized DependencyGraph object.
         */
        static core::DependencyGraph deserialize_dependency_graph(const std::string& json);

        /**
         * Serializes a MetricsSummary object to a JSON string.
         *
         * @param metrics The MetricsSummary object.
         * @return The JSON string representing the metrics.
         */
        static std::string serialize_metrics_summary(const core::MetricsSummary& metrics);

        /**
         * Deserializes a MetricsSummary from a JSON string.
         *
         * @param json JSON string representing the MetricsSummary.
         * @return The deserialized MetricsSummary object.
         */
        static core::MetricsSummary deserialize_metrics_summary(const std::string& json);

        /**
         * Deserializes a Hotspot object from a JSON string.
         *
         * @param json JSON string representing the Hotspot.
         * @return The deserialized Hotspot object.
         */
        static core::Hotspot deserialize_hotspot(const std::string& json);

        /**
         * Deserializes a TemplateHotspot object from a JSON string.
         *
         * @param json JSON string representing the TemplateHotspot.
         * @return The deserialized TemplateHotspot object.
         */
        static core::TemplateHotspot deserialize_template_hotspot(const std::string& json);

        /**
         * Deserializes a Suggestion object from a JSON string.
         *
         * @param json JSON string representing the Suggestion.
         * @return The deserialized Suggestion object.
         */
        static core::Suggestion deserialize_suggestion(const std::string& json);

        /**
         * Utility function to deserialize an array of objects from JSON.
         *
         * @tparam T The type of objects in the array.
         * @param json JSON string representing the array.
         * @param deserializer Function pointer to the deserialization function for type T.
         * @return A vector of deserialized objects.
         */
        template<typename T>
        static std::vector<T> deserialize_array(const std::string& json, core::Result<T> (*deserializer)(const std::string&));
    };

} // namespace bha::parsers

#endif //UNIFIED_FORMAT_H