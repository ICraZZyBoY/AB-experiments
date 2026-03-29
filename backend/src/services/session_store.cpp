#include "session_store.hpp"

#include "utils/security.hpp"

namespace ab_experiments::services {

std::string SessionStore::Create(SessionData session_data) {
    std::string token = "ab_session_" + utils::GenerateRandomHex(24);

    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[token] = std::move(session_data);
    return token;
}

std::optional<SessionData> SessionStore::Find(std::string_view token) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = sessions_.find(std::string(token));
    if (it == sessions_.end()) {
        return std::nullopt;
    }

    return it->second;
}

SessionStore& GetSessionStore() {
    static SessionStore session_store;
    return session_store;
}

}  // namespace ab_experiments::services
