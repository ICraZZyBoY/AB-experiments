#include "metric_calculation_scheduler.hpp"

#include <chrono>
#include <exception>
#include <iostream>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace ab_experiments::components {

MetricCalculationScheduler::MetricCalculationScheduler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : ComponentBase(config, context),
      metric_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ),
      calculation_interval_ms_(config["calculation-interval-ms"].As<int>(5000)) {
    userver::utils::PeriodicTask::Settings settings{
        std::chrono::milliseconds(calculation_interval_ms_),
        userver::utils::Flags<userver::utils::PeriodicTask::Flags>{
            userver::utils::PeriodicTask::Flags::kNow
        }
    };
    settings.task_processor = &context.GetTaskProcessor("main-task-processor");

    periodic_task_.Start(
        "metric-calculation-scheduler",
        settings,
        [this] { RunIteration(); }
    );
}

MetricCalculationScheduler::~MetricCalculationScheduler() {
    periodic_task_.Stop();
}

userver::yaml_config::Schema MetricCalculationScheduler::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(
        R"(
type: object
description: periodic metric result calculator
additionalProperties: false
properties:
    calculation-interval-ms:
        type: integer
        description: interval between automatic metric recalculations in milliseconds
)"
    );
}

void MetricCalculationScheduler::RunIteration() {
    try {
        const auto recalculated = metric_service_.RecalculatePendingMetrics();
        if (recalculated > 0) {
            std::cerr << "metric-calculation-scheduler recalculated " << recalculated
                      << " experiment(s)" << std::endl;
        }
    } catch (const std::exception& exception) {
        std::cerr << "metric-calculation-scheduler failed: " << exception.what() << std::endl;
    }
}

}  // namespace ab_experiments::components
