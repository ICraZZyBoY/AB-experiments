#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace ab_experiments::services {

struct FlagView {
    std::int64_t id;
    std::string key;
    std::string name;
    std::string description;
    std::string value_type;
    std::string default_value;
};

struct ExperimentFlagInput {
    std::int64_t flag_id{0};
    std::string variant_value;
};

struct ExperimentVariantInput {
    std::string key;
    std::string name;
    std::string description;
    std::vector<ExperimentFlagInput> flags;
};

struct CreateExperimentInput {
    std::string name;
    std::string description;
    std::string layer_name;
    std::string layer_description;
    std::int32_t duration_days{14};
    double variant_traffic_percent{0.0};
    std::vector<ExperimentVariantInput> variants;
};

struct ExperimentFlagView {
    std::int64_t flag_id;
    std::string flag_key;
    std::string flag_name;
    std::string flag_description;
    std::string value_type;
    std::string default_value;
    std::string variant_value;
};

struct ExperimentVariantView {
    std::int64_t id;
    std::string key;
    std::string name;
    std::string description;
    double traffic_weight{0.0};
    double traffic_percent{0.0};
    std::vector<ExperimentFlagView> flags;
};

struct ExperimentView {
    std::int64_t id;
    std::int64_t client_service_id;
    std::int64_t experiment_layer_id;
    std::string layer_name;
    std::string name;
    std::string description;
    std::string status;
    std::string salt;
    std::int32_t duration_days{14};
    std::string start_at;
    std::string end_at;
    std::string created_at;
    double variant_traffic_percent{0.0};
    double total_traffic_percent{0.0};
    bool metric_calculation_dirty{false};
    std::string metric_calculation_dirty_reason;
    std::string metric_calculation_last_assignment_at;
    std::string metric_calculation_last_event_at;
    std::string metric_calculation_last_calculated_at;
    std::vector<ExperimentVariantView> variants;
    std::vector<std::string> available_actions;
};

class ExperimentService final {
public:
    explicit ExperimentService(userver::storages::postgres::ClusterPtr pg_cluster);

    std::vector<FlagView> ListFlags(std::int64_t client_service_id) const;

    FlagView CreateFlag(
        std::int64_t client_service_id,
        const std::string& key,
        const std::string& name,
        const std::string& description,
        const std::string& value_type,
        const std::string& default_value
    ) const;

    std::vector<ExperimentView> ListExperiments(std::int64_t client_service_id) const;

    ExperimentView CreateExperiment(
        std::int64_t client_service_id,
        std::int64_t created_by_user_id,
        const CreateExperimentInput& input
    ) const;

    ExperimentView UpdateExperimentStatus(
        std::int64_t client_service_id,
        std::int64_t experiment_id,
        const std::string& action
    ) const;

    static bool CanApproveExperiment();

private:
    ExperimentView GetExperimentById(std::int64_t experiment_id) const;

    void EnsureExperimentSchema() const;

    userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace ab_experiments::services
