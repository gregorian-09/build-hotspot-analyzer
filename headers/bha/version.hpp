//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_VERSION_HPP
#define BUILDTIMEHOTSPOTANALYZER_VERSION_HPP

/**
 * @file version.hpp
 * @brief Build Hotspot Analyzer version information.
 *
 * Provides compile-time version constants and runtime version queries.
 */

namespace bha {

    /**
     * Major version number.
     * Incremented for breaking API changes.
     */
    constexpr int VERSION_MAJOR = 1;

    /**
     * Minor version number.
     * Incremented for new features with backward compatibility.
     */
    constexpr int VERSION_MINOR = 0;

    /**
     * Patch version number.
     * Incremented for bug fixes.
     */
    constexpr int VERSION_PATCH = 0;

    /**
     * Full version string in "major.minor.patch" format.
     */
    constexpr auto VERSION_STRING = "1.0.0";

    /**
     * Project name.
     */
    constexpr auto PROJECT_NAME = "Build Hotspot Analyzer";

    /**
     * Short project name for CLI usage.
     */
    constexpr auto PROJECT_SHORT_NAME = "bha";

}  // namespace bha

#endif //BUILDTIMEHOTSPOTANALYZER_VERSION_HPP