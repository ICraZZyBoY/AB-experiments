#include "config_version_service.hpp"

#include <string>
#include <utility>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/transaction.hpp>

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

namespace {

constexpr std::string_view kStatusInQueue = "IN_QUEUE";
constexpr std::string_view kStatusAddedInConfig = "ADDED_IN_CONFIG";
constexpr std::string_view kStatusRunning = "RUNNING";
constexpr std::string_view kStatusCompleted = "COMPLETED";

ConfigVersionView ReadConfigVersionRow(const postgres::Row& row) {
    return ConfigVersionView{
        row["id"].As<std::int64_t>(),
        row["client_service_id"].As<std::int64_t>(),
        row["version"].As<std::int32_t>(),
        row["created_at"].As<std::string>(),
        row["experiment_count"].As<std::int32_t>(),
        row["config_json"].As<std::string>(),
    };
}

[[noreturn]] void ThrowClientError(const std::string& message) {
    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{message}
    );
}

userver::formats::json::ValueBuilder BuildFlagItem(const postgres::Row& row) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["flag_id"] = row["flag_id"].As<std::int64_t>();
    item["flag_key"] = row["flag_key"].As<std::string>();
    item["flag_name"] = row["flag_name"].As<std::string>();
    item["value_type"] = row["value_type"].As<std::string>();
    item["default_value"] = row["default_value"].As<std::string>();
    item["variant_value"] = row["variant_value"].As<std::string>();
    return item;
}

userver::formats::json::ValueBuilder BuildExperimentSnapshot(
    postgres::Transaction& transaction,
    const postgres::Row& experiment_row
) {
    userver::formats::json::ValueBuilder experiment(userver::formats::common::Type::kObject);
    userver::formats::json::ValueBuilder variants(userver::formats::common::Type::kArray);

    const auto experiment_id = experiment_row["id"].As<std::int64_t>();
    experiment["experiment_id"] = experiment_id;
    experiment["layer_name"] = experiment_row["layer_name"].As<std::string>();
    experiment["name"] = experiment_row["name"].As<std::string>();
    experiment["description"] = experiment_row["description"].As<std::string>();
    experiment["status"] = experiment_row["status"].As<std::string>();
    experiment["salt"] = experiment_row["salt"].As<std::string>();
    experiment["duration_days"] = experiment_row["duration_days"].As<std::int32_t>();
    experiment["start_at"] = experiment_row["start_at"].As<std::string>();
    experiment["end_at"] = experiment_row["end_at"].As<std::string>();

    const auto variants_result = transaction.Execute(
        "SELECT id, key, name, COALESCE(description, '') AS description, traffic_weight "
        "FROM abtest.ExperimentVariant "
        "WHERE experiment_id = $1 "
        "ORDER BY id",
        experiment_id
    );

    for (const auto& variant_row : variants_result) {
        userver::formats::json::ValueBuilder variant(userver::formats::common::Type::kObject);
        userver::formats::json::ValueBuilder flags(userver::formats::common::Type::kArray);

        const auto variant_id = variant_row["id"].As<std::int64_t>();
        variant["variant_id"] = variant_id;
        variant["key"] = variant_row["key"].As<std::string>();
        variant["name"] = variant_row["name"].As<std::string>();
        variant["description"] = variant_row["description"].As<std::string>();
        variant["traffic_weight"] = variant_row["traffic_weight"].As<double>();
        variant["traffic_percent"] = variant_row["traffic_weight"].As<double>() * 100.0;

        const auto flags_result = transaction.Execute(
            "SELECT f.id AS flag_id, f.key AS flag_key, f.name AS flag_name, f.value_type, "
            "COALESCE(f.default_value, '') AS default_value, vf.value AS variant_value "
            "FROM abtest.VariantFlag vf "
            "JOIN abtest.Flag f ON f.id = vf.flag_id "
            "WHERE vf.experiment_variant_id = $1 "
            "ORDER BY f.id",
            variant_id
        );

        for (const auto& flag_row : flags_result) {
            flags.PushBack(BuildFlagItem(flag_row));
        }

        variant["flags"] = flags.ExtractValue();
        variants.PushBack(std::move(variant));
    }

    experiment["variants"] = variants.ExtractValue();
    return experiment;
}

}  // namespace

ConfigVersionService::ConfigVersionService(postgres::ClusterPtr pg_cluster)
    : pg_cluster_(std::move(pg_cluster)) {}

std::vector<ConfigVersionView> ConfigVersionService::ListConfigVersions(
    std::int64_t client_service_id
) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, client_service_id, version, created_at::text AS created_at, "
        "COALESCE(jsonb_array_length(config_json->'experiments'), 0) AS experiment_count, "
        "config_json::text AS config_json "
        "FROM abtest.ConfigVersion "
        "WHERE client_service_id = $1 "
        "ORDER BY version DESC, id DESC",
        client_service_id
    );

    std::vector<ConfigVersionView> versions;
    versions.reserve(result.Size());
    for (const auto& row : result) {
        versions.push_back(ReadConfigVersionRow(row));
    }
    return versions;
}

BuiltConfigVersion ConfigVersionService::BuildNextVersion(
    std::int64_t client_service_id,
    std::int64_t platform_user_id
) const {
    auto transaction =
        pg_cluster_->Begin("build_config_version", postgres::ClusterHostType::kMaster, {});

    const auto next_version_result = transaction.Execute(
        "SELECT COALESCE(MAX(version), 0) + 1 AS next_version "
        "FROM abtest.ConfigVersion "
        "WHERE client_service_id = $1",
        client_service_id
    );
    const auto next_version = next_version_result[0]["next_version"].As<std::int32_t>();

    const auto completed_result = transaction.Execute(
        "UPDATE abtest.Experiment "
        "SET status = $1 "
        "WHERE client_service_id = $2 "
        "  AND status = $3 "
        "  AND end_at IS NOT NULL "
        "  AND end_at <= NOW()",
        std::string{kStatusCompleted},
        client_service_id,
        std::string{kStatusRunning}
    );

    const auto queued_result = transaction.Execute(
        "UPDATE abtest.Experiment "
        "SET status = $1 "
        "WHERE client_service_id = $2 "
        "  AND status = $3",
        std::string{kStatusAddedInConfig},
        client_service_id,
        std::string{kStatusInQueue}
    );

    const auto experiments_result = transaction.Execute(
        "SELECT e.id, el.name AS layer_name, e.name, COALESCE(e.description, '') AS description, "
        "e.status, e.salt, e.duration_days, "
        "COALESCE(e.start_at::text, '') AS start_at, COALESCE(e.end_at::text, '') AS end_at "
        "FROM abtest.Experiment e "
        "JOIN abtest.ExperimentLayer el ON el.id = e.experiment_layer_id "
        "WHERE e.client_service_id = $1 "
        "  AND e.status IN ($2, $3) "
        "ORDER BY e.id",
        client_service_id,
        std::string{kStatusAddedInConfig},
        std::string{kStatusRunning}
    );

    userver::formats::json::ValueBuilder config_json(userver::formats::common::Type::kObject);
    userver::formats::json::ValueBuilder experiments(userver::formats::common::Type::kArray);
    config_json["client_service_id"] = client_service_id;
    config_json["version"] = next_version;

    for (const auto& experiment_row : experiments_result) {
        experiments.PushBack(BuildExperimentSnapshot(transaction, experiment_row));
    }
    config_json["experiments"] = experiments.ExtractValue();

    const auto config_json_string = userver::formats::json::ToString(config_json.ExtractValue());
    const auto inserted_result = transaction.Execute(
        "INSERT INTO abtest.ConfigVersion(client_service_id, version, config_json) "
        "VALUES ($1, $2, $3::jsonb) "
        "RETURNING id",
        client_service_id,
        next_version,
        config_json_string
    );
    const auto config_version_id = inserted_result[0]["id"].As<std::int64_t>();

    transaction.Execute(
        "INSERT INTO abtest.AuditLog("
        "client_service_id, platform_user_id, entity_type, entity_id, action, details"
        ") VALUES ($1, $2, 'ConfigVersion', $3, 'BUILD', $4::jsonb)",
        client_service_id,
        platform_user_id,
        config_version_id,
        std::string("{\"queued_added_count\":") +
            std::to_string(queued_result.RowsAffected()) +
            ",\"completed_removed_count\":" +
            std::to_string(completed_result.RowsAffected()) + "}"
    );

    transaction.Commit();

    BuiltConfigVersion built;
    built.config_version = GetConfigVersionById(config_version_id);
    built.queued_added_count = static_cast<std::int32_t>(queued_result.RowsAffected());
    built.completed_removed_count = static_cast<std::int32_t>(completed_result.RowsAffected());
    return built;
}

std::vector<BuiltConfigVersion> ConfigVersionService::BuildPendingVersions() const {
    const auto services_result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT cs.id AS client_service_id, "
        "COALESCE((SELECT MIN(pu.id) FROM abtest.PlatformUser pu "
        "          WHERE pu.client_service_id = cs.id), 0) AS platform_user_id "
        "FROM abtest.ClientService cs "
        "WHERE EXISTS ("
        "    SELECT 1 FROM abtest.Experiment e "
        "    WHERE e.client_service_id = cs.id AND e.status = $1"
        ") OR EXISTS ("
        "    SELECT 1 FROM abtest.Experiment e "
        "    WHERE e.client_service_id = cs.id "
        "      AND e.status = $2 "
        "      AND e.end_at IS NOT NULL "
        "      AND e.end_at <= NOW()"
        ") "
        "ORDER BY cs.id",
        std::string{kStatusInQueue},
        std::string{kStatusRunning}
    );

    std::vector<BuiltConfigVersion> built_versions;
    built_versions.reserve(services_result.Size());

    for (const auto& row : services_result) {
        const auto client_service_id = row["client_service_id"].As<std::int64_t>();
        const auto platform_user_id = row["platform_user_id"].As<std::int64_t>();
        if (platform_user_id <= 0) {
            continue;
        }

        built_versions.push_back(BuildNextVersion(client_service_id, platform_user_id));
    }

    return built_versions;
}

ConfigVersionView ConfigVersionService::GetConfigVersionById(std::int64_t config_version_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, client_service_id, version, created_at::text AS created_at, "
        "COALESCE(jsonb_array_length(config_json->'experiments'), 0) AS experiment_count, "
        "config_json::text AS config_json "
        "FROM abtest.ConfigVersion "
        "WHERE id = $1",
        config_version_id
    );
    if (result.IsEmpty()) {
        ThrowClientError("Config version was not found");
    }

    return ReadConfigVersionRow(result[0]);
}

}  // namespace ab_experiments::services
