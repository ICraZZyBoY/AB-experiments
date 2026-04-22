#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include "components/config_build_scheduler.hpp"
#include "components/metric_calculation_scheduler.hpp"
#include "handlers/api_keys_handler.hpp"
#include "handlers/client_services_handler.hpp"
#include "handlers/config_versions_handler.hpp"
#include "handlers/experiment_metrics_handler.hpp"
#include "handlers/experiments_handler.hpp"
#include "handlers/features_handler.hpp"
#include "handlers/flags_handler.hpp"
#include "handlers/health_handler.hpp"
#include "handlers/login_handler.hpp"
#include "handlers/metric_results_handler.hpp"
#include "handlers/metrics_handler.hpp"
#include "handlers/platform_users_handler.hpp"
#include "handlers/register_service_handler.hpp"
#include "handlers/runtime_events_handler.hpp"
#include "handlers/runtime_flags_handler.hpp"

int main(int argc, char* argv[]) {
    auto component_list = userver::components::MinimalServerComponentList()
        .Append<userver::clients::dns::Component>()
        .Append<userver::components::TestsuiteSupport>("testsuite-support")
        .Append<ab_experiments::components::ConfigBuildScheduler>()
        .Append<ab_experiments::components::MetricCalculationScheduler>()
        .Append<userver::components::Postgres>("postgres-db")
        .Append<ab_experiments::handlers::ApiKeysHandler>()
        .Append<ab_experiments::handlers::ClientServicesHandler>()
        .Append<ab_experiments::handlers::ConfigVersionsHandler>()
        .Append<ab_experiments::handlers::ExperimentMetricsHandler>()
        .Append<ab_experiments::handlers::ExperimentsHandler>()
        .Append<ab_experiments::handlers::FeaturesHandler>()
        .Append<ab_experiments::handlers::FlagsHandler>()
        .Append<ab_experiments::handlers::HealthHandler>()
        .Append<ab_experiments::handlers::LoginHandler>()
        .Append<ab_experiments::handlers::MetricResultsHandler>()
        .Append<ab_experiments::handlers::MetricsHandler>()
        .Append<ab_experiments::handlers::PlatformUsersHandler>()
        .Append<ab_experiments::handlers::RegisterServiceHandler>()
        .Append<ab_experiments::handlers::RuntimeEventsHandler>()
        .Append<ab_experiments::handlers::RuntimeFlagsHandler>()
        .Append<userver::server::handlers::ServerMonitor>();

    return userver::utils::DaemonMain(argc, argv, component_list);
}
