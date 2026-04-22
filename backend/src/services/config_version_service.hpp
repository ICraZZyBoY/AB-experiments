#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace ab_experiments::services {

struct ConfigVersionView {
    std::int64_t id;
    std::int64_t client_service_id;
    std::int32_t version;
    std::string created_at;
    std::int32_t experiment_count;
    std::string config_json;
};

struct BuiltConfigVersion {
    ConfigVersionView config_version;
    std::int32_t queued_added_count{0};
    std::int32_t completed_removed_count{0};
};

class ConfigVersionService final {
public:
    explicit ConfigVersionService(userver::storages::postgres::ClusterPtr pg_cluster);

    std::vector<ConfigVersionView> ListConfigVersions(std::int64_t client_service_id) const;

    BuiltConfigVersion BuildNextVersion(
        std::int64_t client_service_id,
        std::int64_t platform_user_id
    ) const;

    std::vector<BuiltConfigVersion> BuildPendingVersions() const;

private:
    ConfigVersionView GetConfigVersionById(std::int64_t config_version_id) const;

    userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace ab_experiments::services
