#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace ab_experiments::services {

struct AssignmentRequestInput {
    std::string req_id;
    std::string external_user_id;
    std::string device_id;
    std::string ip_address;
    std::string test_id;
};

struct AssignedFlagValue {
    std::int64_t flag_id;
    std::string flag_key;
    std::string flag_name;
    std::string value_type;
    std::string default_value;
    std::string value;
    std::int64_t experiment_id;
    std::int64_t variant_id;
    std::string layer_name;
};

struct AssignmentResultItem {
    std::int64_t experiment_id;
    std::string experiment_name;
    std::string layer_name;
    std::int64_t variant_id;
    std::string variant_key;
    std::string variant_name;
};

struct RuntimeFlagsResult {
    std::int32_t config_version{0};
    std::int64_t end_user_id{0};
    std::vector<AssignmentResultItem> assignments;
    std::vector<AssignedFlagValue> flags;
};

class AssignmentService final {
public:
    explicit AssignmentService(userver::storages::postgres::ClusterPtr pg_cluster);

    RuntimeFlagsResult ResolveFlags(
        std::int64_t client_service_id,
        const AssignmentRequestInput& input
    ) const;

private:
    userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace ab_experiments::services
