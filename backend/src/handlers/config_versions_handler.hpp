#pragma once

#include <string_view>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

#include "services/config_version_service.hpp"

namespace ab_experiments::handlers {

class ConfigVersionsHandler final : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "handler-config-versions";

    ConfigVersionsHandler(
        const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& context
    );

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext& context
    ) const override;

private:
    services::ConfigVersionService config_version_service_;
};

}  // namespace ab_experiments::handlers
