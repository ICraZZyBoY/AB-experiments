#include "security.hpp"

#include <array>
#include <random>
#include <sstream>

#include <userver/crypto/hash.hpp>

namespace ab_experiments::utils {

namespace {

std::string ComputePasswordDigest(std::string_view salt, std::string_view password) {
    return userver::crypto::hash::Sha256({salt, ":", password});
}

}  // namespace

std::string GenerateRandomHex(std::size_t bytes_count) {
    static constexpr std::array<char, 16> kHexAlphabet{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };

    std::random_device random_device;
    std::uniform_int_distribution<int> distribution(0, 255);

    std::string result;
    result.reserve(bytes_count * 2);

    for (std::size_t index = 0; index < bytes_count; ++index) {
        const auto byte = static_cast<unsigned char>(distribution(random_device));
        result.push_back(kHexAlphabet[(byte >> 4U) & 0x0FU]);
        result.push_back(kHexAlphabet[byte & 0x0FU]);
    }

    return result;
}

std::string HashPassword(std::string_view password) {
    const std::string salt = GenerateRandomHex(16);
    const std::string digest = ComputePasswordDigest(salt, password);
    return "sha256$" + salt + "$" + digest;
}

bool VerifyPassword(std::string_view password, std::string_view stored_password_hash) {
    static constexpr std::string_view kPrefix = "sha256$";
    if (stored_password_hash.substr(0, kPrefix.size()) != kPrefix) {
        return false;
    }

    const auto salt_start = kPrefix.size();
    const auto separator_position = stored_password_hash.find('$', salt_start);
    if (separator_position == std::string_view::npos) {
        return false;
    }

    const auto salt = stored_password_hash.substr(salt_start, separator_position - salt_start);
    const auto expected_digest = stored_password_hash.substr(separator_position + 1);

    return ComputePasswordDigest(salt, password) == expected_digest;
}

std::string HashApiKey(std::string_view api_key) {
    return userver::crypto::hash::Sha256(api_key);
}

std::string GeneratePlainApiKey() {
    return "ab_live_" + GenerateRandomHex(24);
}

}  // namespace ab_experiments::utils
