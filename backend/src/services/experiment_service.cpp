#include "experiment_service.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "metric_calculation_state.hpp"
#include "utils/security.hpp"

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

namespace {

constexpr double kTrafficWeightTolerance = 1e-6;
constexpr double kPercentDivider = 100.0;

constexpr std::string_view kStatusDraft = "DRAFT";
constexpr std::string_view kStatusInQueue = "IN_QUEUE";
constexpr std::string_view kStatusAddedInConfig = "ADDED_IN_CONFIG";
constexpr std::string_view kStatusRunning = "RUNNING";
constexpr std::string_view kStatusCompleted = "COMPLETED";
constexpr std::string_view kStatusPendingDecision = "PENDING_DECISION";
constexpr std::string_view kStatusClosed = "CLOSED";

constexpr std::string_view kActionSendToQueue = "send_to_queue";
constexpr std::string_view kActionAddToConfig = "add_to_config";
constexpr std::string_view kActionStart = "start";
constexpr std::string_view kActionComplete = "complete";
constexpr std::string_view kActionRequestDecision = "request_decision";
constexpr std::string_view kActionClose = "close";

struct ExperimentLifecycleTransition {
    std::string_view action;
    std::string_view from_status;
    std::string_view to_status;
};

constexpr ExperimentLifecycleTransition kTransitions[] = {
    {kActionSendToQueue, kStatusDraft, kStatusInQueue},
    {kActionAddToConfig, kStatusInQueue, kStatusAddedInConfig},
    {kActionStart, kStatusAddedInConfig, kStatusRunning},
    {kActionComplete, kStatusRunning, kStatusCompleted},
    {kActionRequestDecision, kStatusCompleted, kStatusPendingDecision},
    {kActionClose, kStatusPendingDecision, kStatusClosed},
};

const ExperimentLifecycleTransition* FindTransition(
    std::string_view current_status,
    std::string_view action
) {
    for (const auto& transition : kTransitions) {
        if (transition.from_status == current_status && transition.action == action) {
            return &transition;
        }
    }
    return nullptr;
}

std::vector<std::string> GetAvailableActions(std::string_view status) {
    std::vector<std::string> actions;
    for (const auto& transition : kTransitions) {
        if (transition.from_status == status) {
            actions.emplace_back(transition.action);
        }
    }
    return actions;
}

double WeightToPercent(double traffic_weight) {
    return traffic_weight * kPercentDivider;
}

double PercentToWeight(double traffic_percent) {
    return traffic_percent / kPercentDivider;
}

FlagView ReadFlagRow(const postgres::Row& row) {
    return FlagView{
        row["id"].As<std::int64_t>(),
        row["key"].As<std::string>(),
        row["name"].As<std::string>(),
        row["description"].As<std::string>(),
        row["value_type"].As<std::string>(),
        row["default_value"].As<std::string>(),
    };
}

ExperimentFlagView ReadExperimentFlagRow(const postgres::Row& row) {
    return ExperimentFlagView{
        row["flag_id"].As<std::int64_t>(),
        row["flag_key"].As<std::string>(),
        row["flag_name"].As<std::string>(),
        row["flag_description"].As<std::string>(),
        row["value_type"].As<std::string>(),
        row["default_value"].As<std::string>(),
        row["variant_value"].As<std::string>(),
    };
}

ExperimentVariantView ReadVariantRow(const postgres::Row& row) {
    const auto traffic_weight = row["traffic_weight"].As<double>();
    return ExperimentVariantView{
        row["id"].As<std::int64_t>(),
        row["key"].As<std::string>(),
        row["name"].As<std::string>(),
        row["description"].As<std::string>(),
        traffic_weight,
        WeightToPercent(traffic_weight),
        {},
    };
}

ExperimentView ReadExperimentRow(const postgres::Row& row) {
    return ExperimentView{
        row["id"].As<std::int64_t>(),
        row["client_service_id"].As<std::int64_t>(),
        row["experiment_layer_id"].As<std::int64_t>(),
        row["layer_name"].As<std::string>(),
        row["name"].As<std::string>(),
        row["description"].As<std::string>(),
        row["status"].As<std::string>(),
        row["salt"].As<std::string>(),
        row["duration_days"].As<std::int32_t>(),
        row["start_at"].As<std::string>(),
        row["end_at"].As<std::string>(),
        row["created_at"].As<std::string>(),
        0.0,
        0.0,
        row["metric_calculation_dirty"].As<bool>(),
        row["metric_calculation_dirty_reason"].As<std::string>(),
        row["metric_calculation_last_assignment_at"].As<std::string>(),
        row["metric_calculation_last_event_at"].As<std::string>(),
        row["metric_calculation_last_calculated_at"].As<std::string>(),
        {},
        {},
    };
}

[[noreturn]] void ThrowClientError(const std::string& message) {
    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{message}
    );
}

void ValidateFlagInput(
    const std::string& key,
    const std::string& name,
    const std::string& value_type
) {
    if (key.empty()) {
        ThrowClientError("Field 'key' must not be empty");
    }
    if (name.empty()) {
        ThrowClientError("Field 'name' must not be empty");
    }
    if (value_type.empty()) {
        ThrowClientError("Field 'value_type' must not be empty");
    }
}

void FinalizeExperimentView(ExperimentView& experiment) {
    double total_traffic_percent = 0.0;
    for (const auto& variant : experiment.variants) {
        total_traffic_percent += variant.traffic_percent;
    }

    experiment.total_traffic_percent = total_traffic_percent;
    experiment.variant_traffic_percent =
        experiment.variants.empty() ? 0.0 : experiment.variants.front().traffic_percent;
    experiment.available_actions = GetAvailableActions(experiment.status);
}

void ValidateExperimentInput(const CreateExperimentInput& input) {
    if (input.name.empty()) {
        ThrowClientError("Field 'name' must not be empty");
    }
    if (input.layer_name.empty()) {
        ThrowClientError("Field 'layer_name' must not be empty");
    }
    if (input.duration_days <= 0) {
        ThrowClientError("Field 'duration_days' must be a positive integer");
    }
    if (input.variant_traffic_percent <= 0.0) {
        ThrowClientError("Field 'variant_traffic_percent' must be a positive number");
    }
    if (input.variants.size() < 2) {
        ThrowClientError("Experiment must contain at least two variants");
    }

    const auto total_traffic_percent =
        static_cast<double>(input.variants.size()) * input.variant_traffic_percent;
    if (total_traffic_percent > kPercentDivider + kTrafficWeightTolerance) {
        ThrowClientError("Total experiment traffic percent must not exceed 100");
    }

    std::unordered_set<std::string> variant_keys;
    for (const auto& variant : input.variants) {
        if (variant.key.empty()) {
            ThrowClientError("Every variant must have a non-empty 'key'");
        }
        if (variant.name.empty()) {
            ThrowClientError("Every variant must have a non-empty 'name'");
        }
        if (!variant_keys.insert(variant.key).second) {
            ThrowClientError("Variant keys must be unique within one experiment");
        }
        if (variant.flags.empty()) {
            ThrowClientError("Every variant must contain at least one flag");
        }

        std::unordered_set<std::int64_t> variant_flag_ids;
        for (const auto& flag : variant.flags) {
            if (flag.flag_id <= 0) {
                ThrowClientError("Every variant flag must reference a positive flag_id");
            }
            if (flag.variant_value.empty()) {
                ThrowClientError("Every variant flag must have non-empty 'variant_value'");
            }
            if (!variant_flag_ids.insert(flag.flag_id).second) {
                ThrowClientError("Flag IDs must be unique within one variant");
            }
        }
    }
}

std::int64_t GetOrCreateExperimentLayer(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    const std::string& name,
    const std::string& description
) {
    const auto existing_layer = transaction.Execute(
        "SELECT id "
        "FROM abtest.ExperimentLayer "
        "WHERE client_service_id = $1 AND name = $2",
        client_service_id,
        name
    );
    if (!existing_layer.IsEmpty()) {
        return existing_layer[0]["id"].As<std::int64_t>();
    }

    const auto created_layer = transaction.Execute(
        "INSERT INTO abtest.ExperimentLayer(client_service_id, name, description) "
        "VALUES ($1, $2, NULLIF($3, '')) "
        "RETURNING id",
        client_service_id,
        name,
        description
    );
    return created_layer[0]["id"].As<std::int64_t>();
}

std::int64_t EnsureFlagBelongsToService(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t flag_id
) {
    const auto existing_flag = transaction.Execute(
        "SELECT id "
        "FROM abtest.Flag "
        "WHERE client_service_id = $1 AND id = $2",
        client_service_id,
        flag_id
    );
    if (existing_flag.IsEmpty()) {
        ThrowClientError("Flag with the given id does not exist in this service");
    }
    return existing_flag[0]["id"].As<std::int64_t>();
}

}  // namespace

ExperimentService::ExperimentService(postgres::ClusterPtr pg_cluster)
    : pg_cluster_(std::move(pg_cluster)) {
    EnsureExperimentSchema();
}

std::vector<FlagView> ExperimentService::ListFlags(std::int64_t client_service_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, key, name, COALESCE(description, '') AS description, "
        "value_type, COALESCE(default_value, '') AS default_value "
        "FROM abtest.Flag "
        "WHERE client_service_id = $1 "
        "ORDER BY id",
        client_service_id
    );

    std::vector<FlagView> flags;
    flags.reserve(result.Size());
    for (const auto& row : result) {
        flags.push_back(ReadFlagRow(row));
    }
    return flags;
}

FlagView ExperimentService::CreateFlag(
    std::int64_t client_service_id,
    const std::string& key,
    const std::string& name,
    const std::string& description,
    const std::string& value_type,
    const std::string& default_value
) const {
    ValidateFlagInput(key, name, value_type);

    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "INSERT INTO abtest.Flag(client_service_id, key, name, description, value_type, default_value) "
        "VALUES ($1, $2, $3, NULLIF($4, ''), $5, NULLIF($6, '')) "
        "RETURNING id, key, name, COALESCE(description, '') AS description, "
        "value_type, COALESCE(default_value, '') AS default_value",
        client_service_id,
        key,
        name,
        description,
        value_type,
        default_value
    );

    return ReadFlagRow(result[0]);
}

std::vector<ExperimentView> ExperimentService::ListExperiments(std::int64_t client_service_id) const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    const auto experiments_result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT e.id, e.client_service_id, e.experiment_layer_id, el.name AS layer_name, "
        "e.name, COALESCE(e.description, '') AS description, e.status, e.salt, e.duration_days, "
        "COALESCE(e.start_at::text, '') AS start_at, COALESCE(e.end_at::text, '') AS end_at, "
        "e.created_at::text AS created_at, "
        "COALESCE(emcs.dirty, FALSE) AS metric_calculation_dirty, "
        "COALESCE(emcs.dirty_reason, '') AS metric_calculation_dirty_reason, "
        "COALESCE(to_char(emcs.last_assignment_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS metric_calculation_last_assignment_at, "
        "COALESCE(to_char(emcs.last_event_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS metric_calculation_last_event_at, "
        "COALESCE(to_char(emcs.last_calculated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS metric_calculation_last_calculated_at "
        "FROM abtest.Experiment e "
        "JOIN abtest.ExperimentLayer el ON el.id = e.experiment_layer_id "
        "LEFT JOIN abtest.ExperimentMetricCalculationState emcs ON emcs.experiment_id = e.id "
        "WHERE e.client_service_id = $1 "
        "ORDER BY e.id DESC",
        client_service_id
    );

    std::vector<ExperimentView> experiments;
    experiments.reserve(experiments_result.Size());
    for (const auto& experiment_row : experiments_result) {
        auto experiment = ReadExperimentRow(experiment_row);

        const auto variants_result = pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "SELECT id, key, name, COALESCE(description, '') AS description, traffic_weight "
            "FROM abtest.ExperimentVariant "
            "WHERE experiment_id = $1 "
            "ORDER BY id",
            experiment.id
        );

        experiment.variants.reserve(variants_result.Size());
        for (const auto& variant_row : variants_result) {
            auto variant = ReadVariantRow(variant_row);

            const auto flags_result = pg_cluster_->Execute(
                postgres::ClusterHostType::kMaster,
                "SELECT f.id AS flag_id, f.key AS flag_key, f.name AS flag_name, "
                "COALESCE(f.description, '') AS flag_description, f.value_type, "
                "COALESCE(f.default_value, '') AS default_value, vf.value AS variant_value "
                "FROM abtest.VariantFlag vf "
                "JOIN abtest.Flag f ON f.id = vf.flag_id "
                "WHERE vf.experiment_variant_id = $1 "
                "ORDER BY f.id",
                variant.id
            );

            variant.flags.reserve(flags_result.Size());
            for (const auto& flag_row : flags_result) {
                variant.flags.push_back(ReadExperimentFlagRow(flag_row));
            }

            experiment.variants.push_back(std::move(variant));
        }

        FinalizeExperimentView(experiment);
        experiments.push_back(std::move(experiment));
    }

    return experiments;
}

ExperimentView ExperimentService::CreateExperiment(
    std::int64_t client_service_id,
    std::int64_t created_by_user_id,
    const CreateExperimentInput& input
) const {
    ValidateExperimentInput(input);

    auto transaction =
        pg_cluster_->Begin("create_experiment", postgres::ClusterHostType::kMaster, {});

    const auto experiment_layer_id = GetOrCreateExperimentLayer(
        transaction,
        client_service_id,
        input.layer_name,
        input.layer_description
    );

    const auto generated_salt = utils::GenerateExperimentSalt();
    const auto experiment_result = transaction.Execute(
        "INSERT INTO abtest.Experiment("
        "client_service_id, experiment_layer_id, created_by_user_id, "
        "name, description, status, salt, duration_days"
        ") VALUES ($1, $2, $3, $4, NULLIF($5, ''), $6, $7, $8) "
        "RETURNING id",
        client_service_id,
        experiment_layer_id,
        created_by_user_id,
        input.name,
        input.description,
        std::string{kStatusDraft},
        generated_salt,
        input.duration_days
    );
    const auto experiment_id = experiment_result[0]["id"].As<std::int64_t>();
    const auto traffic_weight = PercentToWeight(input.variant_traffic_percent);

    for (const auto& variant_input : input.variants) {
        const auto variant_result = transaction.Execute(
            "INSERT INTO abtest.ExperimentVariant(experiment_id, key, name, description, traffic_weight) "
            "VALUES ($1, $2, $3, NULLIF($4, ''), $5) "
            "RETURNING id",
            experiment_id,
            variant_input.key,
            variant_input.name,
            variant_input.description,
            traffic_weight
        );
        const auto variant_id = variant_result[0]["id"].As<std::int64_t>();

        for (const auto& flag_input : variant_input.flags) {
            const auto flag_id =
                EnsureFlagBelongsToService(transaction, client_service_id, flag_input.flag_id);

            transaction.Execute(
                "INSERT INTO abtest.VariantFlag(experiment_variant_id, flag_id, value) "
                "VALUES ($1, $2, $3)",
                variant_id,
                flag_id,
                flag_input.variant_value
            );
        }
    }

    transaction.Commit();
    return GetExperimentById(experiment_id);
}

ExperimentView ExperimentService::UpdateExperimentStatus(
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    const std::string& action
) const {
    const auto experiment_result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, status, duration_days "
        "FROM abtest.Experiment "
        "WHERE client_service_id = $1 AND id = $2",
        client_service_id,
        experiment_id
    );
    if (experiment_result.IsEmpty()) {
        ThrowClientError("Experiment was not found");
    }

    const auto current_status = experiment_result[0]["status"].As<std::string>();
    const auto duration_days = experiment_result[0]["duration_days"].As<std::int32_t>();
    const auto* transition = FindTransition(current_status, action);
    if (!transition) {
        ThrowClientError("Unsupported lifecycle action for the current experiment status");
    }

    if (transition->to_status == kStatusRunning) {
        pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "UPDATE abtest.Experiment "
            "SET status = $1, "
            "start_at = COALESCE(start_at, NOW()), "
            "end_at = COALESCE(end_at, NOW() + $2::interval) "
            "WHERE client_service_id = $3 AND id = $4",
            std::string{transition->to_status},
            std::to_string(duration_days) + " days",
            client_service_id,
            experiment_id
        );
    } else if (transition->to_status == kStatusCompleted) {
        pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "UPDATE abtest.Experiment "
            "SET status = $1, end_at = COALESCE(end_at, NOW()) "
            "WHERE client_service_id = $2 AND id = $3",
            std::string{transition->to_status},
            client_service_id,
            experiment_id
        );
    } else {
        pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "UPDATE abtest.Experiment "
            "SET status = $1 "
            "WHERE client_service_id = $2 AND id = $3",
            std::string{transition->to_status},
            client_service_id,
            experiment_id
        );
    }

    return GetExperimentById(experiment_id);
}

bool ExperimentService::CanApproveExperiment() {
    return true;
}

ExperimentView ExperimentService::GetExperimentById(std::int64_t experiment_id) const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT e.id, e.client_service_id, e.experiment_layer_id, el.name AS layer_name, "
        "e.name, COALESCE(e.description, '') AS description, e.status, e.salt, e.duration_days, "
        "COALESCE(e.start_at::text, '') AS start_at, COALESCE(e.end_at::text, '') AS end_at, "
        "e.created_at::text AS created_at, "
        "COALESCE(emcs.dirty, FALSE) AS metric_calculation_dirty, "
        "COALESCE(emcs.dirty_reason, '') AS metric_calculation_dirty_reason, "
        "COALESCE(to_char(emcs.last_assignment_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS metric_calculation_last_assignment_at, "
        "COALESCE(to_char(emcs.last_event_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS metric_calculation_last_event_at, "
        "COALESCE(to_char(emcs.last_calculated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS metric_calculation_last_calculated_at "
        "FROM abtest.Experiment e "
        "JOIN abtest.ExperimentLayer el ON el.id = e.experiment_layer_id "
        "LEFT JOIN abtest.ExperimentMetricCalculationState emcs ON emcs.experiment_id = e.id "
        "WHERE e.id = $1",
        experiment_id
    );
    if (result.IsEmpty()) {
        ThrowClientError("Experiment was not found");
    }

    auto experiment = ReadExperimentRow(result[0]);
    const auto variants_result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, key, name, COALESCE(description, '') AS description, traffic_weight "
        "FROM abtest.ExperimentVariant "
        "WHERE experiment_id = $1 "
        "ORDER BY id",
        experiment.id
    );
    experiment.variants.reserve(variants_result.Size());
    for (const auto& variant_row : variants_result) {
        auto variant = ReadVariantRow(variant_row);

        const auto flags_result = pg_cluster_->Execute(
            postgres::ClusterHostType::kMaster,
            "SELECT f.id AS flag_id, f.key AS flag_key, f.name AS flag_name, "
            "COALESCE(f.description, '') AS flag_description, f.value_type, "
            "COALESCE(f.default_value, '') AS default_value, vf.value AS variant_value "
            "FROM abtest.VariantFlag vf "
            "JOIN abtest.Flag f ON f.id = vf.flag_id "
            "WHERE vf.experiment_variant_id = $1 "
            "ORDER BY f.id",
            variant.id
        );

        variant.flags.reserve(flags_result.Size());
        for (const auto& flag_row : flags_result) {
            variant.flags.push_back(ReadExperimentFlagRow(flag_row));
        }

        experiment.variants.push_back(std::move(variant));
    }

    FinalizeExperimentView(experiment);
    return experiment;
}

void ExperimentService::EnsureExperimentSchema() const {
    pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "ALTER TABLE abtest.Experiment "
        "ADD COLUMN IF NOT EXISTS duration_days INT NOT NULL DEFAULT 14"
    );
}

}  // namespace ab_experiments::services
