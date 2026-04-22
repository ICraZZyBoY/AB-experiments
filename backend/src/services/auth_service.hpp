#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

#include "session_store.hpp"

namespace ab_experiments::services {

struct PlatformUserView {
    std::int64_t id;
    std::int64_t client_service_id;
    std::string client_service_name;
    std::string email;
    std::string full_name;
    std::string status;
    std::vector<std::string> role_codes;
};

struct ApiKeyView {
    std::int64_t id;
    std::string name;
    std::string status;
    std::string created_at;
};

struct AuthResponse {
    std::string session_token;
    PlatformUserView user;
};

struct LoginServiceOption {
    std::int64_t client_service_id;
    std::string client_service_name;
};

struct LoginAttemptResult {
    std::optional<AuthResponse> auth_response;
    std::vector<LoginServiceOption> service_options;
};

struct CreatedApiKey {
    ApiKeyView api_key;
    std::string plain_api_key;
};

struct ApiKeyIdentity {
    std::int64_t api_key_id;
    std::int64_t client_service_id;
    std::string name;
};

class AuthService final {
public:
    AuthService(
        userver::storages::postgres::ClusterPtr pg_cluster,
        SessionStore& session_store
    );

    AuthResponse RegisterService(
        const std::string& service_name,
        const std::string& service_description,
        const std::string& admin_email,
        const std::string& admin_full_name,
        const std::string& password
    ) const;

    LoginAttemptResult Login(
        const std::string& email,
        const std::string& password,
        std::optional<std::int64_t> client_service_id = std::nullopt
    ) const;

    PlatformUserView CreatePlatformUser(
        std::int64_t client_service_id,
        const std::string& email,
        const std::string& full_name,
        const std::string& password,
        const std::string& role_code
    ) const;

    std::vector<PlatformUserView> ListPlatformUsers(std::int64_t client_service_id) const;

    CreatedApiKey CreateApiKey(std::int64_t client_service_id, const std::string& name) const;

    std::vector<ApiKeyView> ListApiKeys(std::int64_t client_service_id) const;

    ApiKeyView RevokeApiKey(std::int64_t client_service_id, std::int64_t api_key_id) const;

    ApiKeyIdentity AuthenticateApiKey(std::string_view plain_api_key) const;

private:
    AuthResponse CreateAuthResponse(std::int64_t user_id) const;

    PlatformUserView GetPlatformUserById(std::int64_t user_id) const;

    void EnsureDefaultRoles() const;

    std::string CreateSessionToken(const PlatformUserView& user) const;

    userver::storages::postgres::ClusterPtr pg_cluster_;
    SessionStore& session_store_;
};

}  // namespace ab_experiments::services
