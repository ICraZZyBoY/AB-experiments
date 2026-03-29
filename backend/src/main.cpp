#include <userver/clients/dns/component.hpp>
#include <userver/components/minimal_server_component_list.hpp>
#include <userver/server/handlers/server_monitor.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/utils/daemon_run.hpp>

#include "handlers/api_keys_handler.hpp"
#include "handlers/client_services_handler.hpp"
#include "handlers/health_handler.hpp"
#include "handlers/login_handler.hpp"
#include "handlers/platform_users_handler.hpp"
#include "handlers/register_service_handler.hpp"

int main(int argc, char* argv[]) {
    auto component_list = userver::components::MinimalServerComponentList()
        .Append<userver::clients::dns::Component>()
        .Append<userver::components::TestsuiteSupport>("testsuite-support")
        .Append<userver::components::Postgres>("postgres-db")
        .Append<ab_experiments::handlers::ApiKeysHandler>()
        .Append<ab_experiments::handlers::ClientServicesHandler>()
        .Append<ab_experiments::handlers::HealthHandler>()
        .Append<ab_experiments::handlers::LoginHandler>()
        .Append<ab_experiments::handlers::PlatformUsersHandler>()
        .Append<ab_experiments::handlers::RegisterServiceHandler>()
        .Append<userver::server::handlers::ServerMonitor>();

    return userver::utils::DaemonMain(argc, argv, component_list);
}
