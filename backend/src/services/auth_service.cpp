#include "auth_service.hpp"

#include <utility>

#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "utils/http_utils.hpp"
#include "utils/security.hpp"

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

namespace {

struct LoginCandidate {
    std::int64_t user_id;
    std::int64_t client_service_id;
    std::string client_service_name;
};

PlatformUserView ReadPlatformUserRow(const postgres::Row& row) {
    return PlatformUserView{
        row["id"].As<std::int64_t>(),
        row["client_service_id"].As<std::int64_t>(),
        row["client_service_name"].As<std::string>(),
        row["email"].As<std::string>(),
        row["full_name"].As<std::string>(),
        row["status"].As<std::string>(),
        utils::SplitCommaSeparated(row["role_codes"].As<std::string>()),
    };
}

ApiKeyView ReadApiKeyRow(const postgres::Row& row) {
    return ApiKeyView{
        row["id"].As<std::int64_t>(),
        row["name"].As<std::string>(),
        row["status"].As<std::string>(),
        row["created_at"].As<std::string>(),
    };
}

}  // namespace

AuthService::AuthService(postgres::ClusterPtr pg_cluster, SessionStore& session_store)
    : pg_cluster_(std::move(pg_cluster)), session_store_(session_store) {
    EnsureDefaultRoles();
}

AuthResponse AuthService::RegisterService(
    const std::string& service_name,
    const std::string& service_description,
    const std::string& admin_email,
    const std::string& admin_full_name,
    const std::string& password
) const {
    EnsureDefaultRoles();

    auto transaction =
        pg_cluster_->Begin("register_service", postgres::ClusterHostType::kMaster, {});

    const auto service_result = transaction.Execute(
        "INSERT INTO abtest.ClientService(name, description) "
        "VALUES ($1, NULLIF($2, '')) "
        "RETURNING id",
        service_name,
        service_description
    );
    const auto client_service_id = service_result[0]["id"].As<std::int64_t>();

    const auto password_hash = utils::HashPassword(password);
    const auto user_result = transaction.Execute(
        "INSERT INTO abtest.PlatformUser(client_service_id, email, full_name, password_hash) "
        "VALUES ($1, $2, NULLIF($3, ''), $4) "
        "RETURNING id",
        client_service_id,
        admin_email,
        admin_full_name,
        password_hash
    );
    const auto user_id = user_result[0]["id"].As<std::int64_t>();

    transaction.Execute(
        "INSERT INTO abtest.PlatformUserRole(platform_user_id, platform_role_id) "
        "SELECT $1, id FROM abtest.PlatformRole WHERE code = 'ADMIN'",
        user_id
    );

    transaction.Commit();

    return CreateAuthResponse(user_id);
}

LoginAttemptResult AuthService::Login(
    const std::string& email,
    const std::string& password,
    std::optional<std::int64_t> client_service_id
) const {
    std::vector<LoginCandidate> matched_candidates;
    if (client_service_id) {
        const auto result = pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "SELECT pu.id, pu.client_service_id, cs.name AS client_service_name, pu.email, "
            "COALESCE(pu.full_name, '') AS full_name, pu.password_hash, pu.status, "
            "COALESCE(string_agg(pr.code, ',' ORDER BY pr.code), '') AS role_codes "
            "FROM abtest.PlatformUser pu "
            "JOIN abtest.ClientService cs ON cs.id = pu.client_service_id "
            "LEFT JOIN abtest.PlatformUserRole pur ON pur.platform_user_id = pu.id "
            "LEFT JOIN abtest.PlatformRole pr ON pr.id = pur.platform_role_id "
            "WHERE pu.client_service_id = $1 AND pu.email = $2 "
            "GROUP BY pu.id, pu.client_service_id, cs.name, pu.email, pu.full_name, "
            "pu.password_hash, pu.status",
            *client_service_id,
            email
        );

        matched_candidates.reserve(result.Size());
        for (const auto& row : result) {
            const auto stored_password_hash = row["password_hash"].As<std::string>();
            if (!utils::VerifyPassword(password, stored_password_hash)) {
                continue;
            }

            matched_candidates.push_back(LoginCandidate{
                row["id"].As<std::int64_t>(),
                row["client_service_id"].As<std::int64_t>(),
                row["client_service_name"].As<std::string>(),
            });
        }
    } else {
        const auto result = pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "SELECT pu.id, pu.client_service_id, cs.name AS client_service_name, pu.email, "
            "COALESCE(pu.full_name, '') AS full_name, pu.password_hash, pu.status, "
            "COALESCE(string_agg(pr.code, ',' ORDER BY pr.code), '') AS role_codes "
            "FROM abtest.PlatformUser pu "
            "JOIN abtest.ClientService cs ON cs.id = pu.client_service_id "
            "LEFT JOIN abtest.PlatformUserRole pur ON pur.platform_user_id = pu.id "
            "LEFT JOIN abtest.PlatformRole pr ON pr.id = pur.platform_role_id "
            "WHERE pu.email = $1 "
            "GROUP BY pu.id, pu.client_service_id, cs.name, pu.email, pu.full_name, "
            "pu.password_hash, pu.status",
            email
        );

        matched_candidates.reserve(result.Size());
        for (const auto& row : result) {
            const auto stored_password_hash = row["password_hash"].As<std::string>();
            if (!utils::VerifyPassword(password, stored_password_hash)) {
                continue;
            }

            matched_candidates.push_back(LoginCandidate{
                row["id"].As<std::int64_t>(),
                row["client_service_id"].As<std::int64_t>(),
                row["client_service_name"].As<std::string>(),
            });
        }
    }

    if (matched_candidates.empty()) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{"Invalid email or password"}
        );
    }

    if (!client_service_id && matched_candidates.size() > 1) {
        LoginAttemptResult login_result;
        login_result.service_options.reserve(matched_candidates.size());

        for (const auto& candidate : matched_candidates) {
            login_result.service_options.push_back(LoginServiceOption{
                candidate.client_service_id,
                candidate.client_service_name,
            });
        }

        return login_result;
    }

    return LoginAttemptResult{CreateAuthResponse(matched_candidates.front().user_id), {}};
}

PlatformUserView AuthService::CreatePlatformUser(
    std::int64_t client_service_id,
    const std::string& email,
    const std::string& full_name,
    const std::string& password,
    const std::string& role_code
) const {
    EnsureDefaultRoles();

    auto transaction =
        pg_cluster_->Begin("create_platform_user", postgres::ClusterHostType::kMaster, {});

    const auto password_hash = utils::HashPassword(password);
    const auto user_result = transaction.Execute(
        "INSERT INTO abtest.PlatformUser(client_service_id, email, full_name, password_hash) "
        "VALUES ($1, $2, NULLIF($3, ''), $4) "
        "RETURNING id",
        client_service_id,
        email,
        full_name,
        password_hash
    );
    const auto user_id = user_result[0]["id"].As<std::int64_t>();

    const auto role_insert_result = transaction.Execute(
        "INSERT INTO abtest.PlatformUserRole(platform_user_id, platform_role_id) "
        "SELECT $1, id FROM abtest.PlatformRole WHERE code = $2",
        user_id,
        role_code
    );

    if (role_insert_result.RowsAffected() == 0) {
        transaction.Rollback();
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{"Unknown role_code"}
        );
    }

    transaction.Commit();
    return GetPlatformUserById(user_id);
}

std::vector<PlatformUserView> AuthService::ListPlatformUsers(std::int64_t client_service_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT pu.id, pu.client_service_id, cs.name AS client_service_name, pu.email, "
        "COALESCE(pu.full_name, '') AS full_name, pu.status, "
        "COALESCE(string_agg(pr.code, ',' ORDER BY pr.code), '') AS role_codes "
        "FROM abtest.PlatformUser pu "
        "JOIN abtest.ClientService cs ON cs.id = pu.client_service_id "
        "LEFT JOIN abtest.PlatformUserRole pur ON pur.platform_user_id = pu.id "
        "LEFT JOIN abtest.PlatformRole pr ON pr.id = pur.platform_role_id "
        "WHERE pu.client_service_id = $1 "
        "GROUP BY pu.id, pu.client_service_id, cs.name, pu.email, pu.full_name, pu.status "
        "ORDER BY pu.id",
        client_service_id
    );

    std::vector<PlatformUserView> users;
    users.reserve(result.Size());

    for (const auto& row : result) {
        users.push_back(ReadPlatformUserRow(row));
    }

    return users;
}

CreatedApiKey AuthService::CreateApiKey(std::int64_t client_service_id, const std::string& name) const {
    const std::string plain_api_key = utils::GeneratePlainApiKey();
    const std::string key_hash = utils::HashApiKey(plain_api_key);

    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "INSERT INTO abtest.ApiKey(client_service_id, name, key_hash) "
        "VALUES ($1, $2, $3) "
        "RETURNING id, name, status, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at",
        client_service_id,
        name,
        key_hash
    );

    return CreatedApiKey{ReadApiKeyRow(result[0]), plain_api_key};
}

std::vector<ApiKeyView> AuthService::ListApiKeys(std::int64_t client_service_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, name, status, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at "
        "FROM abtest.ApiKey "
        "WHERE client_service_id = $1 "
        "ORDER BY id",
        client_service_id
    );

    std::vector<ApiKeyView> api_keys;
    api_keys.reserve(result.Size());

    for (const auto& row : result) {
        api_keys.push_back(ReadApiKeyRow(row));
    }

    return api_keys;
}

ApiKeyView AuthService::RevokeApiKey(std::int64_t client_service_id, std::int64_t api_key_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "UPDATE abtest.ApiKey "
        "SET status = 'REVOKED', revoked_at = NOW() "
        "WHERE id = $1 AND client_service_id = $2 AND status <> 'REVOKED' "
        "RETURNING id, name, status, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at",
        api_key_id,
        client_service_id
    );

    if (result.IsEmpty()) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{
                "API key not found in this service or already revoked"
            }
        );
    }

    return ReadApiKeyRow(result[0]);
}

ApiKeyIdentity AuthService::AuthenticateApiKey(std::string_view plain_api_key) const {
    if (plain_api_key.empty()) {
        throw userver::server::handlers::Unauthorized(
            userver::server::handlers::ExternalBody{"X-API-Key header must not be empty"}
        );
    }

    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, client_service_id, name "
        "FROM abtest.ApiKey "
        "WHERE key_hash = $1 AND status = 'ACTIVE'",
        utils::HashApiKey(plain_api_key)
    );

    if (result.IsEmpty()) {
        throw userver::server::handlers::Unauthorized(
            userver::server::handlers::ExternalBody{"API key is invalid or revoked"}
        );
    }

    return ApiKeyIdentity{
        result[0]["id"].As<std::int64_t>(),
        result[0]["client_service_id"].As<std::int64_t>(),
        result[0]["name"].As<std::string>(),
    };
}

AuthResponse AuthService::CreateAuthResponse(std::int64_t user_id) const {
    auto user = GetPlatformUserById(user_id);
    return AuthResponse{CreateSessionToken(user), std::move(user)};
}

PlatformUserView AuthService::GetPlatformUserById(std::int64_t user_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT pu.id, pu.client_service_id, cs.name AS client_service_name, pu.email, "
        "COALESCE(pu.full_name, '') AS full_name, pu.status, "
        "COALESCE(string_agg(pr.code, ',' ORDER BY pr.code), '') AS role_codes "
        "FROM abtest.PlatformUser pu "
        "JOIN abtest.ClientService cs ON cs.id = pu.client_service_id "
        "LEFT JOIN abtest.PlatformUserRole pur ON pur.platform_user_id = pu.id "
        "LEFT JOIN abtest.PlatformRole pr ON pr.id = pur.platform_role_id "
        "WHERE pu.id = $1 "
        "GROUP BY pu.id, pu.client_service_id, cs.name, pu.email, pu.full_name, pu.status",
        user_id
    );

    if (result.IsEmpty()) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{"Platform user not found"}
        );
    }

    return ReadPlatformUserRow(result[0]);
}

void AuthService::EnsureDefaultRoles() const {
    pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "INSERT INTO abtest.PlatformRole(code, name, description) VALUES "
        "('ADMIN', 'Administrator', 'Full access to service settings'), "
        "('ANALYST', 'Analyst', 'Can inspect experiments and metrics'), "
        "('DEVELOPER', 'Developer', 'Can work with flags and integrations') "
        "ON CONFLICT (code) DO NOTHING"
    );
}

std::string AuthService::CreateSessionToken(const PlatformUserView& user) const {
    SessionData session_data{
        user.id,
        user.client_service_id,
        user.email,
        user.role_codes,
    };
    return session_store_.Create(std::move(session_data));
}

}  // namespace ab_experiments::services
