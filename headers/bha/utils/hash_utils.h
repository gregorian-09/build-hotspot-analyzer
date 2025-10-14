//
// Created by gregorian on 14/10/2025.
//

#ifndef HASH_UTILS_H
#define HASH_UTILS_H

#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

namespace bha::utils
{
    /**
     * Compute the SHA-256 hash of the input data.
     *
     * @param data The input bytes (text or binary) to hash.
     * @return A hexadecimal string representing the SHA-256 digest.
     */
    std::string compute_sha256(std::string_view data);

    /**
     * Compute the SHA-256 hash of a file’s contents.
     *
     * @param path The filesystem path to the input file.
     * @return A hexadecimal string of the SHA-256 digest, or an error indicator (e.g. empty string or exception) if reading fails.
     */
    std::string compute_sha256_file(std::string_view path);

    /**
     * Compute the MD5 hash of the input data.
     *
     * @param data The input bytes (text or binary) to hash.
     * @return A hexadecimal string representing the MD5 digest.
     */
    std::string compute_md5(std::string_view data);

    /**
     * Compute the MD5 hash of a file’s contents.
     *
     * @param path The file path to read and hash.
     * @return A hexadecimal MD5 digest string, or an error indicator if reading fails.
     */
    std::string compute_md5_file(std::string_view path);

    /**
     * Compute a 64-bit hash from the input data (non-cryptographic).
     *
     * @param data The input bytes to hash.
     * @return A 64-bit integer hash value.
     */
    uint64_t compute_hash64(std::string_view data);

    /**
     * Compute a 32-bit hash from the input data (non-cryptographic).
     *
     * @param data The input bytes to hash.
     * @return A 32-bit integer hash value.
     */
    uint32_t compute_hash32(std::string_view data);

    /**
     * Compute a hash of the input and render it in hexadecimal string form.
     *
     * @param data The input bytes to hash.
     * @return A hex string representation of the hash.
     */
    std::string compute_hash_hex(std::string_view data);

    /**
     * Compute the FNV-1a hash (64-bit) of the input data.
     *
     * @param data Input bytes to hash.
     * @return The computed 64-bit FNV-1a hash.
     */
    uint64_t fnv1a_hash(std::string_view data);

    /**
     * Compute the XXHash64 hash of the input data.
     *
     * @param data Input bytes to hash.
     * @return The 64-bit XXHash64 result.
     */
    uint64_t xxhash64(std::string_view data);

    /**
     * Convert a sequence of bytes into a hexadecimal string.
     *
     * @param bytes The input vector of bytes (uint8_t).
     * @return A lowercase (or uppercase, based on convention) hex string.
     */
    std::string to_hex_string(const std::vector<uint8_t>& bytes);

    /**
     * Convert a 64-bit integer into its hexadecimal string representation.
     *
     * @param value The integer to convert.
     * @return A hex string (without “0x” prefix, typically).
     */
    std::string to_hex_string(uint64_t value);

    /**
     * Parse a hexadecimal string into bytes.
     *
     * @param hex A string representing bytes in hex form (even length).
     * @return A vector of bytes, or an empty vector / error state if parsing fails.
     */
    std::vector<uint8_t> from_hex_string(std::string_view hex);

    /**
     * Combine a new value into an existing 64-bit seed via a standard “hash combine” formula.
     *
     * @tparam T The type of the value to hash.
     * @param seed The current 64-bit seed.
     * @param value The object whose hash will be mixed in.
     * @return The updated seed after combining.
     */
    template<typename T>
    uint64_t hash_combine(uint64_t seed, const T& value) {
        std::hash<T> hasher;
        seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }

    /**
     * Generate a new universally unique identifier (UUID) string.
     *
     * @return A UUID string in canonical form (e.g. “xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx”).
     */
    std::string generate_uuid();

    /**
     * Generate a short identifier string of given length, composed of alphanumeric characters.
     *
     * @param length Desired length of the ID (default 8).
     * @return A randomly generated short ID string.
     */
    std::string generate_short_id(size_t length = 8);

} // namespace bha::utils

#endif //HASH_UTILS_H
