#include "event_service.hpp"

#include <string>
#include <utility>

#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "metric_calculation_state.hpp"

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

namespace {

FeatureView ReadFeatureRow(const postgres::Row& row) {
    return FeatureView{
        row["id"].As<std::int64_t>(),
        row["key"].As<std::string>(),
        row["name"].As<std::string>(),
        row["description"].As<std::string>(),
    };
}

[[noreturn]] void ThrowClientError(const std::string& message) {
    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{message}
    );
}

std::int64_t EnsureEndUser(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    const EventInput& input
) {
    if (input.external_user_id.empty() && input.device_id.empty()) {
        ThrowClientError("Either 'external_user_id' or 'device_id' must be provided");
    }

    if (!input.device_id.empty()) {
        const auto lookup_result = transaction.Execute(
            "SELECT id "
            "FROM abtest.EndUser "
            "WHERE client_service_id = $1 AND device_id = $2 "
            "ORDER BY id "
            "LIMIT 1",
            client_service_id,
            input.device_id
        );

        if (!lookup_result.IsEmpty()) {
            const auto end_user_id = lookup_result[0]["id"].As<std::int64_t>();
            transaction.Execute(
                "UPDATE abtest.EndUser "
                "SET external_user_id = COALESCE(NULLIF($2, ''), external_user_id), "
                "    device_id = COALESCE(NULLIF($3, ''), device_id), "
                "    ip_address = COALESCE(NULLIF($4, '')::inet, ip_address) "
                "WHERE id = $1",
                end_user_id,
                input.external_user_id,
                input.device_id,
                input.ip_address
            );
            return end_user_id;
        }
    } else {
        const auto lookup_result = transaction.Execute(
            "SELECT id "
            "FROM abtest.EndUser "
            "WHERE client_service_id = $1 AND external_user_id = $2 "
            "ORDER BY id "
            "LIMIT 1",
            client_service_id,
            input.external_user_id
        );

        if (!lookup_result.IsEmpty()) {
            const auto end_user_id = lookup_result[0]["id"].As<std::int64_t>();
            transaction.Execute(
                "UPDATE abtest.EndUser "
                "SET external_user_id = COALESCE(NULLIF($2, ''), external_user_id), "
                "    device_id = COALESCE(NULLIF($3, ''), device_id), "
                "    ip_address = COALESCE(NULLIF($4, '')::inet, ip_address) "
                "WHERE id = $1",
                end_user_id,
                input.external_user_id,
                input.device_id,
                input.ip_address
            );
            return end_user_id;
        }
    }

    const auto insert_result = transaction.Execute(
        "INSERT INTO abtest.EndUser(client_service_id, external_user_id, device_id, ip_address) "
        "VALUES ($1, NULLIF($2, ''), NULLIF($3, ''), NULLIF($4, '')::inet) "
        "RETURNING id",
        client_service_id,
        input.external_user_id,
        input.device_id,
        input.ip_address
    );
    return insert_result[0]["id"].As<std::int64_t>();
}

}  // namespace

EventService::EventService(postgres::ClusterPtr pg_cluster) : pg_cluster_(std::move(pg_cluster)) {}

FeatureView EventService::CreateFeature(
    std::int64_t client_service_id,
    const std::string& key,
    const std::string& name,
    const std::string& description
) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "INSERT INTO abtest.Feature(client_service_id, key, name, description) "
        "VALUES ($1, $2, $3, NULLIF($4, '')) "
        "RETURNING id, key, name, COALESCE(description, '') AS description",
        client_service_id,
        key,
        name,
        description
    );
    return ReadFeatureRow(result[0]);
}

std::vector<FeatureView> EventService::ListFeatures(std::int64_t client_service_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, key, name, COALESCE(description, '') AS description "
        "FROM abtest.Feature "
        "WHERE client_service_id = $1 "
        "ORDER BY id",
        client_service_id
    );

    std::vector<FeatureView> features;
    features.reserve(result.Size());
    for (const auto& row : result) {
        features.push_back(ReadFeatureRow(row));
    }
    return features;
}

EventIngestResult EventService::IngestEvent(
    std::int64_t client_service_id,
    const EventInput& input
) const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    if (input.feature_key.empty()) {
        ThrowClientError("Field 'feature_key' must not be empty");
    }

    auto transaction =
        pg_cluster_->Begin("ingest_runtime_event", postgres::ClusterHostType::kMaster, {});

    const auto feature_result = transaction.Execute(
        "SELECT id, key "
        "FROM abtest.Feature "
        "WHERE client_service_id = $1 AND key = $2",
        client_service_id,
        input.feature_key
    );
    if (feature_result.IsEmpty()) {
        ThrowClientError("Feature was not found for this service");
    }

    const auto feature_id = feature_result[0]["id"].As<std::int64_t>();
    const auto end_user_id = EnsureEndUser(transaction, client_service_id, input);
    const auto insert_result = transaction.Execute(
        "INSERT INTO abtest.EventsLog("
        "client_service_id, end_user_id, feature_id, req_id, occurred_at, value, properties"
        ") VALUES ("
        "$1, $2, $3, NULLIF($4, ''), COALESCE(NULLIF($5, '')::timestamptz, NOW()), $6, "
        "CASE WHEN NULLIF($7, '') IS NULL THEN NULL ELSE $7::jsonb END"
        ") "
        "RETURNING id, "
        "to_char(occurred_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS occurred_at",
        client_service_id,
        end_user_id,
        feature_id,
        input.req_id,
        input.occurred_at,
        input.value,
        input.properties_json
    );

    const auto occurred_at = insert_result[0]["occurred_at"].As<std::string>();
    MarkExperimentsDirtyByRequest(
        transaction,
        client_service_id,
        end_user_id,
        input.req_id,
        kDirtyReasonEventIngested,
        occurred_at
    );

    transaction.Commit();

    return EventIngestResult{
        insert_result[0]["id"].As<std::int64_t>(),
        end_user_id,
        feature_id,
        feature_result[0]["key"].As<std::string>(),
        occurred_at,
    };
}

}  // namespace ab_experiments::services
