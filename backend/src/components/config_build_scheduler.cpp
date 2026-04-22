#include "config_build_scheduler.hpp"

#include <chrono>
#include <exception>
#include <iostream>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace ab_experiments::components {

ConfigBuildScheduler::ConfigBuildScheduler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : ComponentBase(config, context),
      config_version_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ),
      build_interval_ms_(config["build-interval-ms"].As<int>(5000)) {
    userver::utils::PeriodicTask::Settings settings{
        std::chrono::milliseconds(build_interval_ms_),
        userver::utils::Flags<userver::utils::PeriodicTask::Flags>{
            userver::utils::PeriodicTask::Flags::kNow
        }
    };
    settings.task_processor = &context.GetTaskProcessor("main-task-processor");

    periodic_task_.Start(
        "config-build-scheduler",
        settings,
        [this] { RunIteration(); }
    );
}

ConfigBuildScheduler::~ConfigBuildScheduler() {
    periodic_task_.Stop();
}

userver::yaml_config::Schema ConfigBuildScheduler::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(
        R"(
type: object
description: periodic config version builder
additionalProperties: false
properties:
    build-interval-ms:
        type: integer
        description: interval between automatic config builds in milliseconds
)"
    );
}

void ConfigBuildScheduler::RunIteration() {
    try {
        const auto built_versions = config_version_service_.BuildPendingVersions();
        if (!built_versions.empty()) {
            std::cerr << "config-build-scheduler built " << built_versions.size()
                      << " config version(s)" << std::endl;
        }
    } catch (const std::exception& exception) {
        std::cerr << "config-build-scheduler failed: " << exception.what() << std::endl;
    }
}

}  // namespace ab_experiments::components
