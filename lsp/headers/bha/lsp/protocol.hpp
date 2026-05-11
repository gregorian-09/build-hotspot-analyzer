#pragma once

/**
 * @file protocol.hpp
 * @brief JSON-RPC/LSP protocol envelope types and serializers.
 */

#include "types.hpp"
#include <string>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace bha::lsp
{
    /**
     * @brief JSON-RPC request identifier type.
     *
     * LSP permits numeric and string identifiers.
     */
    using RequestId = std::variant<int, std::string>;

    /**
     * @brief JSON-RPC request envelope.
     */
    struct RequestMessage {
        /// JSON-RPC protocol version (typically `"2.0"`).
        std::string jsonrpc;
        /// Request identifier.
        RequestId id;
        /// Method name.
        std::string method;
        /// Optional method parameters object/array.
        std::optional<json> params;
    };

    /**
     * @brief JSON-RPC response envelope.
     */
    struct ResponseMessage {
        /// JSON-RPC protocol version.
        std::string jsonrpc;
        /// Correlated request identifier.
        RequestId id;
        /// Optional successful result payload.
        std::optional<json> result;
        /// Optional error payload.
        std::optional<json> error;
    };

    /**
     * @brief JSON-RPC notification envelope.
     */
    struct NotificationMessage {
        /// JSON-RPC protocol version.
        std::string jsonrpc;
        /// Method name.
        std::string method;
        /// Optional method parameters object/array.
        std::optional<json> params;
    };

    /**
     * @brief JSON-RPC error object.
     */
    struct ResponseError {
        /// Error code (standard or domain-specific).
        int code;
        /// Human-readable error message.
        std::string message;
        /// Optional structured error details.
        std::optional<json> data;
    };

    /**
     * @brief Standard JSON-RPC/LSP plus BHA-specific error codes.
     */
    enum class ErrorCode {
        // JSON-RPC standard errors
        ParseError = -32700,
        InvalidRequest = -32600,
        MethodNotFound = -32601,
        InvalidParams = -32602,
        InternalError = -32603,

        // LSP standard errors
        ServerNotInitialized = -32002,
        RequestCancelled = -32800,

        // BHA-specific errors
        NoCompilationDatabase = -32001,
        AnalysisFailed = -32002,
        InvalidProject = -32003,
        ValidationFailed = -32010,
        SyntaxError = -32011,
        SemanticError = -32012,
        BuildSystemError = -32013,
        ApplyFailed = -32020,
        BuildFailed = -32021,
        RollbackFailed = -32022,
        InvalidSuggestionId = -32030,
        InvalidBackupId = -32031,
        ConcurrentModification = -32032
    };

    /// Serialize request identifier variant.
    inline void to_json(json& j, const RequestId& id) {
        std::visit([&j](auto&& arg) { j = arg; }, id);
    }

    /// Deserialize request identifier variant.
    inline void from_json(const json& j, RequestId& id) {
        if (j.is_number()) {
            id = j.get<int>();
        } else {
            id = j.get<std::string>();
        }
    }

    /// Serialize `ResponseError`.
    inline void to_json(json& j, const ResponseError& e) {
        j["code"] = e.code;
        j["message"] = e.message;
        if (e.data) {
            j["data"] = *e.data;
        }
    }

    /// Deserialize `ResponseError`.
    inline void from_json(const json& j, ResponseError& e) {
        j.at("code").get_to(e.code);
        j.at("message").get_to(e.message);
        if (j.contains("data")) {
            e.data = j.at("data");
        }
    }

    /// Serialize `RequestMessage`.
    inline void to_json(json& j, const RequestMessage& m) {
        j["jsonrpc"] = m.jsonrpc;
        json id_json;
        to_json(id_json, m.id);
        j["id"] = id_json;
        j["method"] = m.method;
        if (m.params) {
            j["params"] = *m.params;
        }
    }

    /// Deserialize `RequestMessage`.
    inline void from_json(const json& j, RequestMessage& m) {
        j.at("jsonrpc").get_to(m.jsonrpc);
        from_json(j.at("id"), m.id);
        j.at("method").get_to(m.method);
        if (j.contains("params")) {
            m.params = j.at("params");
        }
    }

    /// Serialize `ResponseMessage`.
    inline void to_json(json& j, const ResponseMessage& m) {
        j["jsonrpc"] = m.jsonrpc;
        json id_json;
        to_json(id_json, m.id);
        j["id"] = id_json;
        if (m.result) {
            j["result"] = *m.result;
        }
        if (m.error) {
            j["error"] = *m.error;
        }
    }

    /// Deserialize `ResponseMessage`.
    inline void from_json(const json& j, ResponseMessage& m) {
        j.at("jsonrpc").get_to(m.jsonrpc);
        from_json(j.at("id"), m.id);
        if (j.contains("result")) {
            m.result = j.at("result");
        }
        if (j.contains("error")) {
            m.error = j.at("error");
        }
    }

    /// Serialize `NotificationMessage`.
    inline void to_json(json& j, const NotificationMessage& m) {
        j["jsonrpc"] = m.jsonrpc;
        j["method"] = m.method;
        if (m.params) {
            j["params"] = *m.params;
        }
    }

    /// Deserialize `NotificationMessage`.
    inline void from_json(const json& j, NotificationMessage& m) {
        j.at("jsonrpc").get_to(m.jsonrpc);
        j.at("method").get_to(m.method);
        if (j.contains("params")) {
            m.params = j.at("params");
        }
    }
}
