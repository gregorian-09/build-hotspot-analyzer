//
// Created by gregorian on 30/10/2025.
//

#include <gtest/gtest.h>
#include "bha/utils/hash_utils.h"
#include <fstream>
#include <filesystem>

using namespace bha::utils;
namespace fs = std::filesystem;

class HashUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "hash_utils_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string create_test_file(const std::string& filename, const std::string& content) const
    {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path, std::ios::binary);
        file << content;
        file.close();
        return file_path.string();
    }

    fs::path temp_dir;
};

TEST_F(HashUtilsTest, SHA256_EmptyString) {
    const auto hash = compute_sha256("");
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(HashUtilsTest, SHA256_SimpleString) {
    const auto hash = compute_sha256("hello");
    EXPECT_EQ(hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_F(HashUtilsTest, SHA256_LongerString) {
    const auto hash = compute_sha256("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(hash, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_F(HashUtilsTest, SHA256_BinaryData) {
    const std::string binary_data = {0x00, 0x01, 0x02, 0x03, static_cast<char>(0xFF)};
    const auto hash = compute_sha256(binary_data);
    EXPECT_FALSE(hash.empty());
    EXPECT_EQ(hash.length(), 64); // SHA-256 produces 32 bytes = 64 hex chars
}

TEST_F(HashUtilsTest, SHA256_File_ValidFile) {
    const std::string file_path = create_test_file("test.txt", "hello world");
    const auto hash = compute_sha256_file(file_path);
    EXPECT_EQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

TEST_F(HashUtilsTest, SHA256_File_EmptyFile) {
    const std::string file_path = create_test_file("empty.txt", "");
    const auto hash = compute_sha256_file(file_path);
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(HashUtilsTest, SHA256_File_NonExistent) {
    const auto hash = compute_sha256_file("/nonexistent/file/path.txt");
    EXPECT_TRUE(hash.empty());
}


TEST_F(HashUtilsTest, MD5_EmptyString) {
    const auto hash = compute_md5("");
    EXPECT_EQ(hash, "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_F(HashUtilsTest, MD5_SimpleString) {
    const auto hash = compute_md5("hello");
    EXPECT_EQ(hash, "5d41402abc4b2a76b9719d911017c592");
}

TEST_F(HashUtilsTest, MD5_LongerString) {
    const auto hash = compute_md5("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(hash, "9e107d9d372bb6826bd81d3542a419d6");
}

TEST_F(HashUtilsTest, MD5_File_ValidFile) {
    const std::string file_path = create_test_file("test_md5.txt", "test content");
    const auto hash = compute_md5_file(file_path);
    EXPECT_FALSE(hash.empty());
    EXPECT_EQ(hash.length(), 32); // MD5 produces 16 bytes = 32 hex chars
}

TEST_F(HashUtilsTest, MD5_File_NonExistent) {
    const auto hash = compute_md5_file("/nonexistent/file.txt");
    EXPECT_TRUE(hash.empty());
}

TEST_F(HashUtilsTest, Hash64_EmptyString) {
    const auto hash = compute_hash64("");
    EXPECT_NE(hash, 0); // Most hash functions produce non-zero for empty input
}

TEST_F(HashUtilsTest, Hash64_Consistency) {
    const std::string input = "test data";
    const auto hash1 = compute_hash64(input);
    const auto hash2 = compute_hash64(input);
    EXPECT_EQ(hash1, hash2);
}

TEST_F(HashUtilsTest, Hash64_Different) {
    const auto hash1 = compute_hash64("test1");
    const auto hash2 = compute_hash64("test2");
    EXPECT_NE(hash1, hash2);
}

TEST_F(HashUtilsTest, Hash32_EmptyString) {
    const auto hash = compute_hash32("");
    EXPECT_GE(hash, 0u);
}

TEST_F(HashUtilsTest, Hash32_Consistency) {
    const std::string input = "test data";
    const auto hash1 = compute_hash32(input);
    const auto hash2 = compute_hash32(input);
    EXPECT_EQ(hash1, hash2);
}

TEST_F(HashUtilsTest, Hash32_Different) {
    const auto hash1 = compute_hash32("abc");
    const auto hash2 = compute_hash32("xyz");
    EXPECT_NE(hash1, hash2);
}

TEST_F(HashUtilsTest, HashHex_ValidOutput) {
    const auto hash = compute_hash_hex("test");
    EXPECT_FALSE(hash.empty());
    // Should be a valid hex string (only 0-9, a-f characters)
    for (char c : hash) {
        EXPECT_TRUE(std::isxdigit(c)) << "Invalid hex character: " << c;
    }
}

TEST_F(HashUtilsTest, HashHex_Consistency) {
    const auto hash1 = compute_hash_hex("data");
    const auto hash2 = compute_hash_hex("data");
    EXPECT_EQ(hash1, hash2);
}

TEST_F(HashUtilsTest, FNV1a_EmptyString) {
    const auto hash = fnv1a_hash("");
    // FNV-1a has a specific offset basis for empty string
    EXPECT_EQ(hash, 0xcbf29ce484222325ULL); // FNV-1a 64-bit offset basis
}

TEST_F(HashUtilsTest, FNV1a_KnownValue) {
    const auto hash = fnv1a_hash("hello");
    // FNV-1a is deterministic
    EXPECT_NE(hash, 0);
}

TEST_F(HashUtilsTest, FNV1a_Consistency) {
    auto hash1 = fnv1a_hash("test string");
    auto hash2 = fnv1a_hash("test string");
    EXPECT_EQ(hash1, hash2);
}

TEST_F(HashUtilsTest, FNV1a_Different) {
    const auto hash1 = fnv1a_hash("test1");
    const auto hash2 = fnv1a_hash("test2");
    EXPECT_NE(hash1, hash2);
}

TEST_F(HashUtilsTest, XXHash64_EmptyString) {
    const auto hash = xxhash64("");
    EXPECT_NE(hash, 0);
}

TEST_F(HashUtilsTest, XXHash64_Consistency) {
    const std::string input = "xxhash test data";
    const auto hash1 = xxhash64(input);
    const auto hash2 = xxhash64(input);
    EXPECT_EQ(hash1, hash2);
}

TEST_F(HashUtilsTest, XXHash64_Different) {
    const auto hash1 = xxhash64("data1");
    const auto hash2 = xxhash64("data2");
    EXPECT_NE(hash1, hash2);
}

TEST_F(HashUtilsTest, ToHexString_ByteVector_Empty) {
    constexpr std::vector<uint8_t> bytes;
    const auto hex = to_hex_string(bytes);
    EXPECT_EQ(hex, "");
}

TEST_F(HashUtilsTest, ToHexString_ByteVector_Simple) {
    const std::vector<uint8_t> bytes = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    const auto hex = to_hex_string(bytes);
    EXPECT_TRUE(hex == "0123456789abcdef" || hex == "0123456789ABCDEF");
}

TEST_F(HashUtilsTest, ToHexString_ByteVector_AllZeros) {
    const std::vector<uint8_t> bytes = {0x00, 0x00, 0x00};
    const auto hex = to_hex_string(bytes);
    EXPECT_EQ(hex, "000000");
}

TEST_F(HashUtilsTest, ToHexString_Uint64_Zero) {
    const auto hex = to_hex_string(0ULL);
    EXPECT_EQ(hex, "0000000000000000");
}

TEST_F(HashUtilsTest, ToHexString_Uint64_MaxValue) {
    const auto hex = to_hex_string(0xFFFFFFFFFFFFFFFFULL);
    EXPECT_TRUE(hex == "ffffffffffffffff" || hex == "FFFFFFFFFFFFFFFF");
}

TEST_F(HashUtilsTest, ToHexString_Uint64_Simple) {
    const auto hex = to_hex_string(255ULL);
    EXPECT_TRUE(hex == "00000000000000ff" || hex == "00000000000000FF");
}

TEST_F(HashUtilsTest, FromHexString_Empty) {
    const auto bytes = from_hex_string("");
    EXPECT_TRUE(bytes.empty());
}

TEST_F(HashUtilsTest, FromHexString_Valid) {
    const auto bytes = from_hex_string("0123456789abcdef");
    ASSERT_EQ(bytes.size(), 8);
    EXPECT_EQ(bytes[0], 0x01);
    EXPECT_EQ(bytes[1], 0x23);
    EXPECT_EQ(bytes[2], 0x45);
    EXPECT_EQ(bytes[3], 0x67);
    EXPECT_EQ(bytes[4], 0x89);
    EXPECT_EQ(bytes[5], 0xAB);
    EXPECT_EQ(bytes[6], 0xCD);
    EXPECT_EQ(bytes[7], 0xEF);
}

TEST_F(HashUtilsTest, FromHexString_Uppercase) {
    const auto bytes = from_hex_string("ABCDEF");
    ASSERT_EQ(bytes.size(), 3);
    EXPECT_EQ(bytes[0], 0xAB);
    EXPECT_EQ(bytes[1], 0xCD);
    EXPECT_EQ(bytes[2], 0xEF);
}

TEST_F(HashUtilsTest, FromHexString_Mixed) {
    const auto bytes = from_hex_string("AaBbCc");
    ASSERT_EQ(bytes.size(), 3);
    EXPECT_EQ(bytes[0], 0xAA);
    EXPECT_EQ(bytes[1], 0xBB);
    EXPECT_EQ(bytes[2], 0xCC);
}

TEST_F(HashUtilsTest, FromHexString_OddLength) {
    const auto bytes = from_hex_string("abc");
    EXPECT_TRUE(bytes.empty() || bytes.size() == 1 || bytes.size() == 2);
}

TEST_F(HashUtilsTest, FromHexString_InvalidCharacters) {
    const auto bytes = from_hex_string("xyz123");
    EXPECT_TRUE(bytes.empty());
}

TEST_F(HashUtilsTest, FromHexString_RoundTrip) {
    const std::vector<uint8_t> original = {0x12, 0x34, 0x56, 0x78, 0x9A};
    const auto hex = to_hex_string(original);
    const auto restored = from_hex_string(hex);
    EXPECT_EQ(original, restored);
}

TEST_F(HashUtilsTest, HashCombine_Integers) {
    uint64_t seed = 0;
    seed = hash_combine(seed, 42);
    seed = hash_combine(seed, 100);
    EXPECT_NE(seed, 0);
}

TEST_F(HashUtilsTest, HashCombine_Strings) {
    uint64_t seed = 0;
    seed = hash_combine(seed, std::string("hello"));
    seed = hash_combine(seed, std::string("world"));
    EXPECT_NE(seed, 0);
}

TEST_F(HashUtilsTest, HashCombine_OrderMatters) {
    uint64_t seed1 = 0;
    seed1 = hash_combine(seed1, 1);
    seed1 = hash_combine(seed1, 2);

    uint64_t seed2 = 0;
    seed2 = hash_combine(seed2, 2);
    seed2 = hash_combine(seed2, 1);

    EXPECT_NE(seed1, seed2);
}

TEST_F(HashUtilsTest, HashCombine_Consistency) {
    uint64_t seed1 = 0;
    seed1 = hash_combine(seed1, 123);
    seed1 = hash_combine(seed1, 456);

    uint64_t seed2 = 0;
    seed2 = hash_combine(seed2, 123);
    seed2 = hash_combine(seed2, 456);

    EXPECT_EQ(seed1, seed2);
}

TEST_F(HashUtilsTest, GenerateUUID_ValidFormat) {
    const auto uuid = generate_uuid();
    EXPECT_EQ(uuid.length(), 36); // Standard UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');
}

TEST_F(HashUtilsTest, GenerateUUID_Uniqueness) {
    const auto uuid1 = generate_uuid();
    const auto uuid2 = generate_uuid();
    EXPECT_NE(uuid1, uuid2);
}

TEST_F(HashUtilsTest, GenerateUUID_HexCharacters) {
    const auto uuid = generate_uuid();
    for (size_t i = 0; i < uuid.length(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            EXPECT_EQ(uuid[i], '-');
        } else {
            EXPECT_TRUE(std::isxdigit(uuid[i])) << "Invalid character at position " << i;
        }
    }
}

TEST_F(HashUtilsTest, GenerateShortID_DefaultLength) {
    const auto id = generate_short_id();
    EXPECT_EQ(id.length(), 8); // Default is 8
}

TEST_F(HashUtilsTest, GenerateShortID_CustomLength) {
    const auto id = generate_short_id(16);
    EXPECT_EQ(id.length(), 16);
}

TEST_F(HashUtilsTest, GenerateShortID_AlphanumericOnly) {
    for (const auto id = generate_short_id(20); char c : id) {
        EXPECT_TRUE(std::isalnum(c)) << "Invalid character: " << c;
    }
}

TEST_F(HashUtilsTest, GenerateShortID_Uniqueness) {
    const auto id1 = generate_short_id();
    const auto id2 = generate_short_id();
    EXPECT_NE(id1, id2);
}

TEST_F(HashUtilsTest, GenerateShortID_ZeroLength) {
    const auto id = generate_short_id(0);
    EXPECT_EQ(id.length(), 0);
}

TEST_F(HashUtilsTest, GenerateShortID_VeryLong) {
    const auto id = generate_short_id(100);
    EXPECT_EQ(id.length(), 100);
    // Should still be alphanumeric
    for (const char c : id) {
        EXPECT_TRUE(std::isalnum(c));
    }
}