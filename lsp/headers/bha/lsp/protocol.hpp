#pragma once

#include "types.hpp"
#include <string>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

namespace bha::lsp
{
    using RequestId = std::variant<int, std::string>;

    struct RequestMessage {
        std::string jsonrpc;
        RequestId id;
        std::string method;
        std::optional<json> params;
    };

    struct ResponseMessage {
        std::string jsonrpc;
        RequestId id;
        std::optional<json> result;
        std::optional<json> error;
    };

    struct NotificationMessage {
        std::string jsonrpc;
        std::string method;
        std::optional<json> params;
    };

    struct ResponseError {
        int code;
        std::string message;
        std::optional<json> data;
    };

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

    inline void to_json(json& j, const RequestId& id) {
        std::visit([&j](auto&& arg) { j = arg; }, id);
    }

    inline void from_json(const json& j, RequestId& id) {
        if (j.is_number()) {
            id = j.get<int>();
        } else {
            id = j.get<std::string>();
        }
    }

    inline void to_json(json& j, const ResponseError& e) {
        j["code"] = e.code;
        j["message"] = e.message;
        if (e.data) {
            j["data"] = *e.data;
        }
    }

    inline void from_json(const json& j, ResponseError& e) {
        j.at("code").get_to(e.code);
        j.at("message").get_to(e.message);
        if (j.contains("data")) {
            e.data = j.at("data");
        }
    }

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

    inline void from_json(const json& j, RequestMessage& m) {
        j.at("jsonrpc").get_to(m.jsonrpc);
        from_json(j.at("id"), m.id);
        j.at("method").get_to(m.method);
        if (j.contains("params")) {
            m.params = j.at("params");
        }
    }

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

    inline void to_json(json& j, const NotificationMessage& m) {
        j["jsonrpc"] = m.jsonrpc;
        j["method"] = m.method;
        if (m.params) {
            j["params"] = *m.params;
        }
    }

    inline void from_json(const json& j, NotificationMessage& m) {
        j.at("jsonrpc").get_to(m.jsonrpc);
        j.at("method").get_to(m.method);
        if (j.contains("params")) {
            m.params = j.at("params");
        }
    }
}
