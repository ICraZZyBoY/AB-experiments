#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace ab_experiments::services {

struct MetricView {
    std::int64_t id;
    std::string code;
    std::string name;
    std::string description;
    std::string metric_type;
    std::string aggregation_unit;
    std::string feature_key;
};

struct ExperimentMetricView {
    std::int64_t experiment_id;
    std::int64_t metric_id;
    std::string metric_code;
    std::string metric_name;
    std::string metric_type;
    std::string aggregation_unit;
    std::string feature_key;
    bool is_primary{false};
    bool is_guardrail{false};
};

struct MetricResultView {
    std::int64_t experiment_id;
    std::int64_t metric_id;
    std::string metric_code;
    std::string metric_name;
    std::int64_t variant_id;
    std::string variant_key;
    std::string variant_name;
    std::string period_start;
    std::string period_end;
    double value{0.0};
    std::optional<double> std_error;
    std::optional<double> ci_low;
    std::optional<double> ci_high;
    std::optional<double> p_value;
    std::optional<double> lift;
};

class MetricService final {
public:
    explicit MetricService(userver::storages::postgres::ClusterPtr pg_cluster);

    MetricView CreateMetric(
        std::int64_t client_service_id,
        const std::string& code,
        const std::string& name,
        const std::string& description,
        const std::string& metric_type,
        const std::string& feature_key
    ) const;

    std::vector<MetricView> ListMetrics(std::int64_t client_service_id) const;

    ExperimentMetricView AttachMetricToExperiment(
        std::int64_t client_service_id,
        std::int64_t experiment_id,
        std::int64_t metric_id,
        bool is_primary,
        bool is_guardrail
    ) const;

    std::vector<ExperimentMetricView> ListExperimentMetrics(
        std::int64_t client_service_id
    ) const;

    std::vector<MetricResultView> RecalculateExperimentMetrics(
        std::int64_t client_service_id,
        std::int64_t experiment_id
    ) const;

    std::vector<MetricResultView> ListMetricResults(
        std::int64_t client_service_id,
        std::optional<std::int64_t> experiment_id = std::nullopt
    ) const;

    std::int32_t RecalculatePendingMetrics() const;

private:
    userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace ab_experiments::services
