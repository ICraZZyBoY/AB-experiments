#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace ab_experiments::services {

struct FeatureView {
    std::int64_t id;
    std::string key;
    std::string name;
    std::string description;
};

struct EventInput {
    std::string feature_key;
    std::string req_id;
    std::string external_user_id;
    std::string device_id;
    std::string ip_address;
    std::string occurred_at;
    std::optional<double> value;
    std::string properties_json;
};

struct EventIngestResult {
    std::int64_t event_id;
    std::int64_t end_user_id;
    std::int64_t feature_id;
    std::string feature_key;
    std::string occurred_at;
};

class EventService final {
public:
    explicit EventService(userver::storages::postgres::ClusterPtr pg_cluster);

    FeatureView CreateFeature(
        std::int64_t client_service_id,
        const std::string& key,
        const std::string& name,
        const std::string& description
    ) const;

    std::vector<FeatureView> ListFeatures(std::int64_t client_service_id) const;

    EventIngestResult IngestEvent(std::int64_t client_service_id, const EventInput& input) const;

private:
    userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace ab_experiments::services
