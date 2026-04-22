#pragma once

#include <string>
#include <string_view>

namespace ab_experiments::utils {

std::string GenerateRandomHex(std::size_t bytes_count);

std::string HashPassword(std::string_view password);

bool VerifyPassword(std::string_view password, std::string_view stored_password_hash);

std::string HashApiKey(std::string_view api_key);

std::string GeneratePlainApiKey();

std::string GenerateExperimentSalt();

}  // namespace ab_experiments::utils
