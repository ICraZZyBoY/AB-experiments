#include "metric_service.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "metric_calculation_state.hpp"

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

namespace {

constexpr std::string_view kMetricTypeCount = "COUNT";
constexpr std::string_view kMetricTypeSum = "SUM";
constexpr std::string_view kMetricTypeMean = "MEAN";
constexpr std::string_view kAggregationUnitPerRequest = "PER_REQUEST";

struct VariantAggregateRow {
    std::int64_t variant_id{0};
    std::string variant_key;
    std::string variant_name;
    std::int64_t sample_count{0};
    double sum_value{0.0};
    double sum_squares{0.0};
};

struct VariantComputedStats {
    std::int64_t variant_id{0};
    std::string variant_key;
    std::string variant_name;
    std::int64_t sample_count{0};
    double value{0.0};
    std::optional<double> variance;
    std::optional<double> std_error;
    std::optional<double> ci_low;
    std::optional<double> ci_high;
};

MetricView ReadMetricRow(const postgres::Row& row) {
    return MetricView{
        row["id"].As<std::int64_t>(),
        row["code"].As<std::string>(),
        row["name"].As<std::string>(),
        row["description"].As<std::string>(),
        row["metric_type"].As<std::string>(),
        row["aggregation_unit"].As<std::string>(),
        row["feature_key"].As<std::string>(),
    };
}

ExperimentMetricView ReadExperimentMetricRow(const postgres::Row& row) {
    return ExperimentMetricView{
        row["experiment_id"].As<std::int64_t>(),
        row["metric_id"].As<std::int64_t>(),
        row["metric_code"].As<std::string>(),
        row["metric_name"].As<std::string>(),
        row["metric_type"].As<std::string>(),
        row["aggregation_unit"].As<std::string>(),
        row["feature_key"].As<std::string>(),
        row["is_primary"].As<bool>(),
        row["is_guardrail"].As<bool>(),
    };
}

MetricResultView ReadMetricResultRow(const postgres::Row& row) {
    return MetricResultView{
        row["experiment_id"].As<std::int64_t>(),
        row["metric_id"].As<std::int64_t>(),
        row["metric_code"].As<std::string>(),
        row["metric_name"].As<std::string>(),
        row["variant_id"].As<std::int64_t>(),
        row["variant_key"].As<std::string>(),
        row["variant_name"].As<std::string>(),
        row["period_start"].As<std::string>(),
        row["period_end"].As<std::string>(),
        row["value"].As<double>(),
        row["std_error"].IsNull() ? std::nullopt
                                  : std::optional<double>(row["std_error"].As<double>()),
        row["ci_low"].IsNull() ? std::nullopt : std::optional<double>(row["ci_low"].As<double>()),
        row["ci_high"].IsNull() ? std::nullopt
                                : std::optional<double>(row["ci_high"].As<double>()),
        row["p_value"].IsNull() ? std::nullopt
                                : std::optional<double>(row["p_value"].As<double>()),
        row["lift"].IsNull() ? std::nullopt : std::optional<double>(row["lift"].As<double>()),
    };
}

VariantAggregateRow ReadVariantAggregateRow(const postgres::Row& row) {
    return VariantAggregateRow{
        row["variant_id"].As<std::int64_t>(),
        row["variant_key"].As<std::string>(),
        row["variant_name"].As<std::string>(),
        row["sample_count"].As<std::int64_t>(),
        row["sum_value"].As<double>(),
        row["sum_squares"].As<double>(),
    };
}

[[noreturn]] void ThrowClientError(const std::string& message) {
    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{message}
    );
}

void ValidateMetricType(const std::string& metric_type) {
    static constexpr std::array<std::string_view, 3> kAllowedMetricTypes{
        kMetricTypeCount,
        kMetricTypeSum,
        kMetricTypeMean,
    };

    if (metric_type.empty()) {
        ThrowClientError("Field 'metric_type' must not be empty");
    }

    if (std::find(kAllowedMetricTypes.begin(), kAllowedMetricTypes.end(), metric_type) ==
        kAllowedMetricTypes.end()) {
        ThrowClientError("Unsupported metric_type. Allowed values: COUNT, SUM, MEAN");
    }
}

std::string BuildMetricDefinitionJson(const std::string& feature_key) {
    if (feature_key.empty()) {
        ThrowClientError("Field 'feature_key' must not be empty");
    }

    userver::formats::json::ValueBuilder definition;
    definition["feature_key"] = feature_key;
    return userver::formats::json::ToString(definition.ExtractValue());
}

void EnsureExperimentBelongsToService(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t experiment_id
) {
    const auto result = transaction.Execute(
        "SELECT id "
        "FROM abtest.Experiment "
        "WHERE id = $1 AND client_service_id = $2",
        experiment_id,
        client_service_id
    );
    if (result.IsEmpty()) {
        ThrowClientError("Experiment was not found in this service");
    }
}

std::vector<ExperimentMetricView> ReadExperimentMetricsFromQuery(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::optional<std::int64_t> experiment_id
) {
    const auto result = experiment_id
        ? transaction.Execute(
              "SELECT em.experiment_id, m.id AS metric_id, m.code AS metric_code, "
              "m.name AS metric_name, m.metric_type, m.aggregation_unit, "
              "COALESCE(m.definition::jsonb->>'feature_key', '') AS feature_key, "
              "em.is_primary, em.is_guardrail "
              "FROM abtest.ExperimentMetric em "
              "JOIN abtest.Experiment e ON e.id = em.experiment_id "
              "JOIN abtest.Metric m ON m.id = em.metric_id "
              "WHERE e.client_service_id = $1 AND em.experiment_id = $2 "
              "ORDER BY em.experiment_id, m.id",
              client_service_id,
              *experiment_id
          )
        : transaction.Execute(
              "SELECT em.experiment_id, m.id AS metric_id, m.code AS metric_code, "
              "m.name AS metric_name, m.metric_type, m.aggregation_unit, "
              "COALESCE(m.definition::jsonb->>'feature_key', '') AS feature_key, "
              "em.is_primary, em.is_guardrail "
              "FROM abtest.ExperimentMetric em "
              "JOIN abtest.Experiment e ON e.id = em.experiment_id "
              "JOIN abtest.Metric m ON m.id = em.metric_id "
              "WHERE e.client_service_id = $1 "
              "ORDER BY em.experiment_id, m.id",
              client_service_id
          );

    std::vector<ExperimentMetricView> items;
    items.reserve(result.Size());
    for (const auto& row : result) {
        items.push_back(ReadExperimentMetricRow(row));
    }
    return items;
}

std::vector<VariantAggregateRow> QueryVariantAggregates(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    std::int64_t feature_id,
    std::string_view metric_type
) {
    const std::string aggregate_expression =
        metric_type == kMetricTypeCount
            ? "COALESCE(COUNT(el.id), 0)::double precision"
            : metric_type == kMetricTypeSum
                  ? "COALESCE(SUM(el.value), 0)::double precision"
                  : "COALESCE(AVG(el.value), 0)::double precision";
    const auto query =
        std::string(
            "WITH assignment_requests AS ("
            "  SELECT DISTINCT experiment_variant_id, req_id, end_user_id "
            "  FROM abtest.AssignmentLog "
            "  WHERE experiment_id = $1"
            "), request_samples AS ("
            "  SELECT ar.experiment_variant_id AS variant_id, ar.req_id, ar.end_user_id, "
        ) +
        aggregate_expression +
        " AS sample_value "
        "  FROM assignment_requests ar "
        "  LEFT JOIN abtest.EventsLog el "
        "    ON el.client_service_id = $2 "
        "   AND el.end_user_id = ar.end_user_id "
        "   AND el.req_id = ar.req_id "
        "   AND el.feature_id = $3 "
        "  GROUP BY ar.experiment_variant_id, ar.req_id, ar.end_user_id"
        ") "
        "SELECT ev.id AS variant_id, ev.key AS variant_key, ev.name AS variant_name, "
        "COALESCE(COUNT(rs.req_id), 0) AS sample_count, "
        "COALESCE(SUM(rs.sample_value), 0)::double precision AS sum_value, "
        "COALESCE(SUM(rs.sample_value * rs.sample_value), 0)::double precision AS sum_squares "
        "FROM abtest.ExperimentVariant ev "
        "LEFT JOIN request_samples rs ON rs.variant_id = ev.id "
        "WHERE ev.experiment_id = $1 "
        "GROUP BY ev.id, ev.key, ev.name "
        "ORDER BY ev.id";

    const auto result = transaction.Execute(
        query,
        experiment_id,
        client_service_id,
        feature_id
    );

    std::vector<VariantAggregateRow> rows;
    rows.reserve(result.Size());
    for (const auto& row : result) {
        rows.push_back(ReadVariantAggregateRow(row));
    }
    return rows;
}

VariantComputedStats ComputeVariantStats(const VariantAggregateRow& aggregate) {
    VariantComputedStats stats;
    stats.variant_id = aggregate.variant_id;
    stats.variant_key = aggregate.variant_key;
    stats.variant_name = aggregate.variant_name;
    stats.sample_count = aggregate.sample_count;

    if (aggregate.sample_count <= 0) {
        stats.value = 0.0;
        return stats;
    }

    const auto sample_count_as_double = static_cast<double>(aggregate.sample_count);
    stats.value = aggregate.sum_value / sample_count_as_double;

    if (aggregate.sample_count > 1) {
        const auto raw_variance =
            (aggregate.sum_squares -
             (aggregate.sum_value * aggregate.sum_value) / sample_count_as_double) /
            static_cast<double>(aggregate.sample_count - 1);
        const auto variance = std::max(0.0, raw_variance);
        const auto std_error = std::sqrt(variance / sample_count_as_double);

        stats.variance = variance;
        stats.std_error = std_error;
        stats.ci_low = stats.value - 1.96 * std_error;
        stats.ci_high = stats.value + 1.96 * std_error;
    }

    return stats;
}

std::optional<double> ComputePValue(
    const VariantComputedStats& control,
    const VariantComputedStats& candidate
) {
    if (!control.variance || !candidate.variance || control.sample_count <= 0 ||
        candidate.sample_count <= 0) {
        return std::nullopt;
    }

    const auto variance_term =
        (*control.variance / static_cast<double>(control.sample_count)) +
        (*candidate.variance / static_cast<double>(candidate.sample_count));
    if (variance_term <= 0.0) {
        return control.value == candidate.value ? std::optional<double>(1.0)
                                                : std::optional<double>(0.0);
    }

    const auto z_score = (candidate.value - control.value) / std::sqrt(variance_term);
    return std::erfc(std::abs(z_score) / std::sqrt(2.0));
}

}  // namespace

MetricService::MetricService(postgres::ClusterPtr pg_cluster) : pg_cluster_(std::move(pg_cluster)) {}

MetricView MetricService::CreateMetric(
    std::int64_t client_service_id,
    const std::string& code,
    const std::string& name,
    const std::string& description,
    const std::string& metric_type,
    const std::string& feature_key
) const {
    if (code.empty()) {
        ThrowClientError("Field 'code' must not be empty");
    }
    if (name.empty()) {
        ThrowClientError("Field 'name' must not be empty");
    }
    ValidateMetricType(metric_type);

    auto transaction =
        pg_cluster_->Begin("create_metric", postgres::ClusterHostType::kMaster, {});

    const auto feature_result = transaction.Execute(
        "SELECT id "
        "FROM abtest.Feature "
        "WHERE client_service_id = $1 AND key = $2",
        client_service_id,
        feature_key
    );
    if (feature_result.IsEmpty()) {
        ThrowClientError("Feature was not found for this service");
    }

    const auto result = transaction.Execute(
        "INSERT INTO abtest.Metric("
        "client_service_id, code, name, description, metric_type, aggregation_unit, definition"
        ") VALUES ($1, $2, $3, NULLIF($4, ''), $5, $6, $7) "
        "RETURNING id, code, name, COALESCE(description, '') AS description, "
        "metric_type, aggregation_unit, COALESCE(definition::jsonb->>'feature_key', '') AS feature_key",
        client_service_id,
        code,
        name,
        description,
        metric_type,
        std::string{kAggregationUnitPerRequest},
        BuildMetricDefinitionJson(feature_key)
    );

    transaction.Commit();
    return ReadMetricRow(result[0]);
}

std::vector<MetricView> MetricService::ListMetrics(std::int64_t client_service_id) const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, code, name, COALESCE(description, '') AS description, "
        "metric_type, aggregation_unit, COALESCE(definition::jsonb->>'feature_key', '') AS feature_key "
        "FROM abtest.Metric "
        "WHERE client_service_id = $1 "
        "ORDER BY id",
        client_service_id
    );

    std::vector<MetricView> items;
    items.reserve(result.Size());
    for (const auto& row : result) {
        items.push_back(ReadMetricRow(row));
    }
    return items;
}

ExperimentMetricView MetricService::AttachMetricToExperiment(
    std::int64_t client_service_id,
    std::int64_t experiment_id,
    std::int64_t metric_id,
    bool is_primary,
    bool is_guardrail
) const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    auto transaction =
        pg_cluster_->Begin("attach_metric_to_experiment", postgres::ClusterHostType::kMaster, {});

    EnsureExperimentBelongsToService(transaction, client_service_id, experiment_id);

    const auto metric_check = transaction.Execute(
        "SELECT id "
        "FROM abtest.Metric "
        "WHERE id = $1 AND client_service_id = $2",
        metric_id,
        client_service_id
    );
    if (metric_check.IsEmpty()) {
        ThrowClientError("Metric was not found in this service");
    }

    transaction.Execute(
        "INSERT INTO abtest.ExperimentMetric(experiment_id, metric_id, is_primary, is_guardrail) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (experiment_id, metric_id) DO UPDATE "
        "SET is_primary = EXCLUDED.is_primary, "
        "    is_guardrail = EXCLUDED.is_guardrail",
        experiment_id,
        metric_id,
        is_primary,
        is_guardrail
    );

    MarkExperimentMetricsDirty(
        transaction,
        client_service_id,
        experiment_id,
        kDirtyReasonMetricBindingChanged
    );

    const auto items = ReadExperimentMetricsFromQuery(transaction, client_service_id, experiment_id);
    transaction.Commit();

    const auto it = std::find_if(
        items.begin(),
        items.end(),
        [metric_id](const ExperimentMetricView& item) { return item.metric_id == metric_id; }
    );
    if (it == items.end()) {
        ThrowClientError("Failed to attach metric to experiment");
    }
    return *it;
}

std::vector<ExperimentMetricView> MetricService::ListExperimentMetrics(
    std::int64_t client_service_id
) const {
    auto transaction =
        pg_cluster_->Begin("list_experiment_metrics", postgres::ClusterHostType::kMaster, {});
    auto items = ReadExperimentMetricsFromQuery(transaction, client_service_id, std::nullopt);
    transaction.Commit();
    return items;
}

std::vector<MetricResultView> MetricService::RecalculateExperimentMetrics(
    std::int64_t client_service_id,
    std::int64_t experiment_id
) const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    auto transaction =
        pg_cluster_->Begin("recalculate_experiment_metrics", postgres::ClusterHostType::kMaster, {});

    EnsureExperimentBelongsToService(transaction, client_service_id, experiment_id);

    const auto experiment_result = transaction.Execute(
        "SELECT "
        "to_char(COALESCE(start_at, created_at) AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS period_start, "
        "to_char(NOW() AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS period_end "
        "FROM abtest.Experiment "
        "WHERE id = $1",
        experiment_id
    );
    const auto period_start = experiment_result[0]["period_start"].As<std::string>();
    const auto period_end = experiment_result[0]["period_end"].As<std::string>();

    const auto attached_metrics = ReadExperimentMetricsFromQuery(
        transaction,
        client_service_id,
        experiment_id
    );
    if (attached_metrics.empty()) {
        ThrowClientError("Attach at least one metric to the experiment first");
    }

    transaction.Execute(
        "DELETE FROM abtest.MetricResult "
        "WHERE experiment_id = $1 AND slice_id IS NULL",
        experiment_id
    );

    for (const auto& metric : attached_metrics) {
        const auto feature_result = transaction.Execute(
            "SELECT id "
            "FROM abtest.Feature "
            "WHERE client_service_id = $1 AND key = $2",
            client_service_id,
            metric.feature_key
        );
        if (feature_result.IsEmpty()) {
            ThrowClientError(
                "Metric '" + metric.metric_code + "' references a missing feature"
            );
        }

        const auto feature_id = feature_result[0]["id"].As<std::int64_t>();
        const auto aggregates = QueryVariantAggregates(
            transaction,
            client_service_id,
            experiment_id,
            feature_id,
            metric.metric_type
        );

        std::vector<VariantComputedStats> computed_stats;
        computed_stats.reserve(aggregates.size());
        for (const auto& aggregate : aggregates) {
            computed_stats.push_back(ComputeVariantStats(aggregate));
        }

        const auto control_value =
            computed_stats.empty() ? 0.0 : computed_stats.front().value;
        const auto control_stats =
            computed_stats.empty() ? VariantComputedStats{} : computed_stats.front();

        for (const auto& stats : computed_stats) {
            const auto lift = control_value != 0.0
                ? std::optional<double>((stats.value - control_value) / control_value)
                : std::nullopt;
            const auto p_value =
                stats.variant_id == control_stats.variant_id ? std::optional<double>(1.0)
                                                             : ComputePValue(control_stats, stats);

            transaction.Execute(
                "INSERT INTO abtest.MetricResult("
                "experiment_id, experiment_variant_id, metric_id, slice_id, "
                "period_start, period_end, value, std_error, ci_low, ci_high, p_value, lift"
                ") VALUES ("
                "$1, $2, $3, NULL, $4::timestamptz, $5::timestamptz, $6, $7, $8, $9, $10, $11"
                ")",
                experiment_id,
                stats.variant_id,
                metric.metric_id,
                period_start,
                period_end,
                stats.value,
                stats.std_error,
                stats.ci_low,
                stats.ci_high,
                p_value,
                lift
            );
        }
    }

    MarkExperimentMetricsClean(transaction, client_service_id, experiment_id, period_end);
    transaction.Commit();
    return ListMetricResults(client_service_id, experiment_id);
}

std::vector<MetricResultView> MetricService::ListMetricResults(
    std::int64_t client_service_id,
    std::optional<std::int64_t> experiment_id
) const {
    const auto result = experiment_id
        ? pg_cluster_->Execute(
              postgres::ClusterHostType::kMaster,
              "SELECT mr.experiment_id, mr.metric_id, m.code AS metric_code, m.name AS metric_name, "
              "ev.id AS variant_id, ev.key AS variant_key, ev.name AS variant_name, "
              "to_char(mr.period_start AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS period_start, "
              "to_char(mr.period_end AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS period_end, "
              "mr.value, mr.std_error, mr.ci_low, mr.ci_high, mr.p_value, mr.lift "
              "FROM abtest.MetricResult mr "
              "JOIN abtest.Experiment e ON e.id = mr.experiment_id "
              "JOIN abtest.Metric m ON m.id = mr.metric_id "
              "JOIN abtest.ExperimentVariant ev ON ev.id = mr.experiment_variant_id "
              "WHERE e.client_service_id = $1 AND mr.experiment_id = $2 "
              "ORDER BY mr.experiment_id, mr.metric_id, ev.id",
              client_service_id,
              *experiment_id
          )
        : pg_cluster_->Execute(
              postgres::ClusterHostType::kMaster,
              "SELECT mr.experiment_id, mr.metric_id, m.code AS metric_code, m.name AS metric_name, "
              "ev.id AS variant_id, ev.key AS variant_key, ev.name AS variant_name, "
              "to_char(mr.period_start AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS period_start, "
              "to_char(mr.period_end AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS period_end, "
              "mr.value, mr.std_error, mr.ci_low, mr.ci_high, mr.p_value, mr.lift "
              "FROM abtest.MetricResult mr "
              "JOIN abtest.Experiment e ON e.id = mr.experiment_id "
              "JOIN abtest.Metric m ON m.id = mr.metric_id "
              "JOIN abtest.ExperimentVariant ev ON ev.id = mr.experiment_variant_id "
              "WHERE e.client_service_id = $1 "
              "ORDER BY mr.experiment_id, mr.metric_id, ev.id",
              client_service_id
          );

    std::vector<MetricResultView> items;
    items.reserve(result.Size());
    for (const auto& row : result) {
        items.push_back(ReadMetricResultRow(row));
    }
    return items;
}

std::int32_t MetricService::RecalculatePendingMetrics() const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    const auto experiments = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT DISTINCT e.client_service_id, e.id AS experiment_id "
        "FROM abtest.Experiment e "
        "JOIN abtest.ExperimentMetric em ON em.experiment_id = e.id "
        "LEFT JOIN abtest.ExperimentMetricCalculationState emcs "
        "  ON emcs.experiment_id = e.id "
        "WHERE e.status NOT IN ('DRAFT', 'IN_QUEUE') "
        "  AND ("
        "      emcs.experiment_id IS NULL "
        "      OR emcs.dirty = TRUE "
        "      OR emcs.last_calculated_at IS NULL "
        "  ) "
        "ORDER BY e.client_service_id, e.id"
    );

    std::int32_t recalculated_count = 0;
    for (const auto& row : experiments) {
        RecalculateExperimentMetrics(
            row["client_service_id"].As<std::int64_t>(),
            row["experiment_id"].As<std::int64_t>()
        );
        ++recalculated_count;
    }

    return recalculated_count;
}

}  // namespace ab_experiments::services
