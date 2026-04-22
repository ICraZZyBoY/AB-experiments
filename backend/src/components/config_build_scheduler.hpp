#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/yaml_config/schema.hpp>

#include "services/config_version_service.hpp"

namespace ab_experiments::components {

class ConfigBuildScheduler final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "config-build-scheduler";

    ConfigBuildScheduler(
        const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& context
    );

    ~ConfigBuildScheduler() override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    void RunIteration();

    services::ConfigVersionService config_version_service_;
    userver::utils::PeriodicTask periodic_task_;
    int build_interval_ms_{5000};
};

}  // namespace ab_experiments::components
