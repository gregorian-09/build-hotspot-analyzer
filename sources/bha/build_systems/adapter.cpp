//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/build_systems/adapter.hpp"
#include "bha/build_systems/adapter_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tuple>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace bha::build_systems
{
    using namespace detail;

    // --------------------------------------------------------------------------
    // BuildSystemRegistry
    // --------------------------------------------------------------------------

    BuildSystemRegistry& BuildSystemRegistry::instance() {
        static BuildSystemRegistry registry;
        return registry;
    }

    void BuildSystemRegistry::register_adapter(
        std::unique_ptr<IBuildSystemAdapter> adapter
    ) {
        if (adapter) {
            const std::string adapter_name = adapter->name();
            if (get(adapter_name) != nullptr) {
                return;
            }
            adapters_.push_back(std::move(adapter));
        }
    }

    IBuildSystemAdapter* BuildSystemRegistry::detect(const fs::path& project_path) const {
        IBuildSystemAdapter* best = nullptr;
        double best_confidence = 0.0;
        IBuildSystemAdapter* best_available = nullptr;
        double best_available_confidence = 0.0;

        for (const auto& adapter : adapters_) {
            const double confidence = adapter->detect(project_path);
            if (confidence > best_confidence) {
                best_confidence = confidence;
                best = adapter.get();
            }
            if (confidence > best_available_confidence && adapter_tool_available(*adapter)) {
                best_available_confidence = confidence;
                best_available = adapter.get();
            }
        }

        if (best_available_confidence > 0.0) {
            return best_available;
        }
        return best_confidence > 0.0 ? best : nullptr;
    }

    IBuildSystemAdapter* BuildSystemRegistry::get(const std::string& name) const {
        std::string name_lower = name;
        std::ranges::transform(name_lower, name_lower.begin(),
                               [](const unsigned char c) { return std::tolower(c); });

        for (const auto& adapter : adapters_) {
            std::string adapter_name = adapter->name();
            std::ranges::transform(adapter_name, adapter_name.begin(),
                                   [](const unsigned char c) { return std::tolower(c); });

            if (adapter_name == name_lower) {
                return adapter.get();
            }
        }
        return nullptr;
    }

    // --------------------------------------------------------------------------

}  // namespace bha::build_systems
