//
// Created by gregorian on 14/10/2025.
//

#include "bha/utils/hash_utils.h"
#include "bha/utils/file_utils.h"
#include <iomanip>
#include <sstream>
#include <random>
#include <chrono>
#include <array>
#include <cstring>

#if __has_include(<openssl/sha.h>) && __has_include(<openssl/md5.h>)
    #include <openssl/evp.h>
    #include <openssl/md5.h>
    #define HAS_OPENSSL 1
#endif

namespace bha::utils {

#ifdef HAS_OPENSSL

    std::string compute_hash(const EVP_MD* md, const std::string_view data) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }

        if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestInit_ex failed");
        }

        if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestUpdate failed");
        }

        if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestFinal_ex failed");
        }

        EVP_MD_CTX_free(ctx);

        std::ostringstream ss;
        for (unsigned int i = 0; i < hash_len; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    std::string compute_sha256(const std::string_view data) {
        return compute_hash(EVP_sha256(), data);
    }

    std::string compute_md5(const std::string_view data) {
        return compute_hash(EVP_md5(), data);
    }

    std::string compute_hash_file(const EVP_MD* md, const std::string_view path) {
        const auto content = read_binary_file(path);
        if (!content) {
            return "";
        }
        return compute_hash(md, content->data());
    }

    std::string compute_sha256_file(const std::string_view path) {
        return compute_hash_file(EVP_sha256(), path);
    }

    std::string compute_md5_file(const std::string_view path) {
        return compute_hash_file(EVP_md5(), path);
    }

#else

std::string compute_sha256(const std::string_view data) {
    return to_hex_string(fnv1a_hash(data));
}

std::string compute_sha256_file(const std::string_view path) {
    const auto content = read_file(path);
    if (!content) {
        return "";
    }
    return compute_sha256(*content);
}

std::string compute_md5(const std::string_view data) {
    return to_hex_string(fnv1a_hash(data));
}

std::string compute_md5_file(const std::string_view path) {
    const auto content = read_file(path);
    if (!content) {
        return "";
    }
    return compute_md5(*content);
}

#endif

uint64_t compute_hash64(const std::string_view data) {
    return fnv1a_hash(data);
}

uint32_t compute_hash32(const std::string_view data) {
    const uint64_t hash64 = fnv1a_hash(data);
    return static_cast<uint32_t>(hash64 ^ (hash64 >> 32));
}

std::string compute_hash_hex(const std::string_view data) {
    return to_hex_string(compute_hash64(data));
}

uint64_t fnv1a_hash(const std::string_view data) {
    constexpr uint64_t FNV_offset_basis = 14695981039346656037ULL;

    uint64_t hash = FNV_offset_basis;
    for (const char c : data) {
        constexpr uint64_t FNV_prime = 1099511628211ULL;
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= FNV_prime;
    }
    return hash;
}

uint64_t xxhash64(const std::string_view data) {
    constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
    constexpr uint64_t PRIME64_2 = 14029467366897019727ULL;
    constexpr uint64_t PRIME64_3 = 1609587929392839161ULL;
    constexpr uint64_t PRIME64_5 = 2870177450012600261ULL;

    uint64_t hash = PRIME64_5;
    auto ptr = reinterpret_cast<const uint8_t*>(data.data());
    size_t remaining = data.size();

    while (remaining >= 8) {
        constexpr uint64_t PRIME64_4 = 9650029242287828579ULL;
        uint64_t k;
        std::memcpy(&k, ptr, sizeof(k));
        k *= PRIME64_2;
        k = (k << 31) | (k >> 33);
        k *= PRIME64_1;
        hash ^= k;
        hash = ((hash << 27) | (hash >> 37)) * PRIME64_1 + PRIME64_4;

        ptr += 8;
        remaining -= 8;
    }

    while (remaining > 0) {
        hash ^= static_cast<uint64_t>(*ptr) * PRIME64_5;
        hash = ((hash << 11) | (hash >> 53)) * PRIME64_1;
        ++ptr;
        --remaining;
    }

    hash ^= data.size();
    hash ^= hash >> 33;
    hash *= PRIME64_2;
    hash ^= hash >> 29;
    hash *= PRIME64_3;
    hash ^= hash >> 32;

    return hash;
}

std::string to_hex_string(const std::vector<uint8_t>& bytes) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (const uint8_t b : bytes) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

std::string to_hex_string(const uint64_t value) {
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
}

std::vector<uint8_t> from_hex_string(const std::string_view hex) {
    std::vector<uint8_t> bytes;

    if (hex.size() % 2 != 0) {
        return bytes;
    }

    for (size_t i = 0; i < hex.size(); i += 2) {
        std::string byte_str(hex.substr(i, 2));
        auto byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    const uint64_t part1 = dis(gen);
    const uint64_t part2 = dis(gen);

    std::array<uint8_t, 16> bytes{};
    std::memcpy(&bytes[0], &part1, 8);
    std::memcpy(&bytes[8], &part2, 8);

    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');

    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            ss << '-';
        }
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }

    return ss.str();
}

std::string generate_short_id(const size_t length) {
    static constexpr char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    std::string id;
    id.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        id += alphanum[dis(gen)];
    }

    return id;
}

} // namespace bha::utils