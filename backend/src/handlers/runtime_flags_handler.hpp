#pragma once

#include <string_view>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

#include "services/assignment_service.hpp"
#include "services/auth_service.hpp"

namespace ab_experiments::handlers {

class RuntimeFlagsHandler final : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "handler-runtime-flags";

    RuntimeFlagsHandler(
        const userver::components::ComponentConfig& config,
        const userver::components::ComponentContext& context
    );

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext& context
    ) const override;

private:
    services::AuthService auth_service_;
    services::AssignmentService assignment_service_;
};

}  // namespace ab_experiments::handlers
