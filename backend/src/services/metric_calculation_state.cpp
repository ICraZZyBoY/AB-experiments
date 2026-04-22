#include "metric_calculation_state.hpp"

#include <utility>

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

void EnsureMetricCalculationStateSchema(const postgres::ClusterPtr& pg_cluster) {
    pg_cluster->Execute(
        postgres::ClusterHostType::kMaster,
        "CREATE TABLE IF NOT EXISTS abtest.ExperimentMetricCalculationState ("
        "    experiment_id BIGINT PRIMARY KEY REFERENCES abtest.Experiment(id), "
        "    client_service_id BIGINT NOT NULL REFERENCES abtest.ClientService(id), "
        "    dirty BOOLEAN NOT NULL DEFAULT TRUE, "
        "    dirty_reason TEXT, "
        "    last_source_update_at TIMESTAMPTZ, "
        "    last_assignment_at TIMESTAMPTZ, "
        "    last_event_at TIMESTAMPTZ, "
        "    last_calculated_at TIMESTAMPTZ, "
        "    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()"
        ")"
    );
    pg_cluster->Execute(
        postgres::ClusterHostType::kMaster,
        "ALTER TABLE abtest.ExperimentMetricCalculationState "
        "ADD COLUMN IF NOT EXISTS dirty_reason TEXT"
    );
    pg_cluster->Execute(
        postgres::ClusterHostType::kMaster,
        "ALTER TABLE abtest.ExperimentMetricCalculationState "
        "ADD COLUMN IF NOT EXISTS last_assignment_at TIMESTAMPTZ"
    );
    pg_cluster->Execute(
        postgres::ClusterHostType::kMaster,
        "ALTER TABLE abtest.ExperimentMetricCalculationState "
        "ADD COLUMN IF NOT EXISTS last_event_at TIMESTAMPTZ"
    );
    pg_cluster->Execute(
        postgres::ClusterHostType::kMaster,
        "CREATE INDEX IF NOT EXISTS idx_metriccalcstate_client_dirty "
        "ON abtest.ExperimentMetricCalculationState(client_service_id, dirty)"
    );
}

void MarkExperimentMetricsDirty(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    const std::string& dirty_reason,
    const std::string& source_timestamp
) {
    transaction.Execute(
        "INSERT INTO abtest.ExperimentMetricCalculationState("
        "experiment_id, client_service_id, dirty, dirty_reason, "
        "last_source_update_at, last_assignment_at, updated_at"
        ") VALUES ("
        "$1, $2, TRUE, $3, COALESCE(NULLIF($4, '')::timestamptz, NOW()), "
        "CASE WHEN $3 = 'assignment_created' "
        "     THEN COALESCE(NULLIF($4, '')::timestamptz, NOW()) "
        "     ELSE NULL END, "
        "NOW()"
        ") "
        "ON CONFLICT (experiment_id) DO UPDATE "
        "SET dirty = TRUE, "
        "    dirty_reason = EXCLUDED.dirty_reason, "
        "    last_source_update_at = GREATEST("
        "        COALESCE(abtest.ExperimentMetricCalculationState.last_source_update_at, '-infinity'::timestamptz), "
        "        EXCLUDED.last_source_update_at"
        "    ), "
        "    last_assignment_at = CASE "
        "        WHEN EXCLUDED.last_assignment_at IS NULL "
        "            THEN abtest.ExperimentMetricCalculationState.last_assignment_at "
        "        ELSE GREATEST("
        "            COALESCE(abtest.ExperimentMetricCalculationState.last_assignment_at, '-infinity'::timestamptz), "
        "            EXCLUDED.last_assignment_at"
        "        ) "
        "    END, "
        "    updated_at = NOW()",
        experiment_id,
        client_service_id,
        dirty_reason,
        source_timestamp
    );
}

void MarkExperimentsDirtyByRequest(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t end_user_id,
    const std::string& req_id,
    const std::string& dirty_reason,
    const std::string& source_timestamp
) {
    if (req_id.empty()) {
        return;
    }

    transaction.Execute(
        "INSERT INTO abtest.ExperimentMetricCalculationState("
        "experiment_id, client_service_id, dirty, dirty_reason, "
        "last_source_update_at, last_event_at, updated_at"
        ") "
        "SELECT DISTINCT al.experiment_id, al.client_service_id, TRUE, $4, "
        "       COALESCE(NULLIF($5, '')::timestamptz, NOW()), "
        "       COALESCE(NULLIF($5, '')::timestamptz, NOW()), NOW() "
        "FROM abtest.AssignmentLog al "
        "WHERE al.client_service_id = $1 "
        "  AND al.end_user_id = $2 "
        "  AND al.req_id = $3 "
        "ON CONFLICT (experiment_id) DO UPDATE "
        "SET dirty = TRUE, "
        "    dirty_reason = EXCLUDED.dirty_reason, "
        "    last_source_update_at = GREATEST("
        "        COALESCE(abtest.ExperimentMetricCalculationState.last_source_update_at, '-infinity'::timestamptz), "
        "        EXCLUDED.last_source_update_at"
        "    ), "
        "    last_event_at = GREATEST("
        "        COALESCE(abtest.ExperimentMetricCalculationState.last_event_at, '-infinity'::timestamptz), "
        "        EXCLUDED.last_event_at"
        "    ), "
        "    updated_at = NOW()",
        client_service_id,
        end_user_id,
        req_id,
        dirty_reason,
        source_timestamp
    );
}

void MarkExperimentMetricsClean(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    const std::string& calculated_at
) {
    transaction.Execute(
        "INSERT INTO abtest.ExperimentMetricCalculationState("
        "experiment_id, client_service_id, dirty, dirty_reason, last_calculated_at, updated_at"
        ") VALUES ("
        "$1, $2, FALSE, NULL, COALESCE(NULLIF($3, '')::timestamptz, NOW()), NOW()"
        ") "
        "ON CONFLICT (experiment_id) DO UPDATE "
        "SET dirty = FALSE, "
        "    dirty_reason = NULL, "
        "    last_calculated_at = COALESCE(NULLIF($3, '')::timestamptz, NOW()), "
        "    updated_at = NOW()",
        experiment_id,
        client_service_id,
        calculated_at
    );
}

}  // namespace ab_experiments::services
