#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ab_experiments::services {

struct SessionData {
    std::int64_t user_id;
    std::int64_t client_service_id;
    std::string email;
    std::vector<std::string> role_codes;
};

class SessionStore final {
public:
    std::string Create(SessionData session_data);

    std::optional<SessionData> Find(std::string_view token) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionData> sessions_;
};

SessionStore& GetSessionStore();

}  // namespace ab_experiments::services
