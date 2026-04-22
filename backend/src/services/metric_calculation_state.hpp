#pragma once

#include <cstdint>
#include <string>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

namespace ab_experiments::services {

inline constexpr const char* kDirtyReasonMetricBindingChanged = "metric_binding_changed";
inline constexpr const char* kDirtyReasonAssignmentCreated = "assignment_created";
inline constexpr const char* kDirtyReasonEventIngested = "event_ingested";

void EnsureMetricCalculationStateSchema(
    const userver::storages::postgres::ClusterPtr& pg_cluster
);

void MarkExperimentMetricsDirty(
    userver::storages::postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    const std::string& dirty_reason,
    const std::string& source_timestamp = {}
);

void MarkExperimentsDirtyByRequest(
    userver::storages::postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t end_user_id,
    const std::string& req_id,
    const std::string& dirty_reason,
    const std::string& source_timestamp = {}
);

void MarkExperimentMetricsClean(
    userver::storages::postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    const std::string& calculated_at = {}
);

}  // namespace ab_experiments::services
