#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/schema.hpp>

#include "services/metric_service.hpp"

namespace ab_experiments::components {

class MetricCalculationScheduler final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "metric-calculation-scheduler";

    MetricCalculationScheduler(
        const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& context
    );

    ~MetricCalculationScheduler() override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    void RunIteration();

    services::MetricService metric_service_;
    userver::utils::PeriodicTask periodic_task_;
    int calculation_interval_ms_{5000};
};

}  // namespace ab_experiments::components
