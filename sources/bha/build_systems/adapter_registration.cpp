#include "bha/build_systems/adapter.hpp"

namespace bha::build_systems {

    void register_core_adapters() {
        register_cmake_adapter();
        register_msbuild_adapter();
    }

    void register_experimental_adapters() {
        register_ninja_adapter();
        register_make_adapter();
        register_meson_adapter();
        register_bazel_adapter();
        register_buck2_adapter();
        register_scons_adapter();
        register_xcode_adapter();
    }

    void register_all_adapters(const AdapterRegistrationMode mode) {
        register_core_adapters();
        if (mode == AdapterRegistrationMode::IncludeExperimental) {
            register_experimental_adapters();
        }
    }

}  // namespace bha::build_systems
