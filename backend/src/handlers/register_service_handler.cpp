#include "register_service_handler.hpp"

#include <userver/formats/json/value_builder.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

RegisterServiceHandler::RegisterServiceHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      auth_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster(),
          services::GetSessionStore()
      ) {}

std::string RegisterServiceHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto json = utils::ParseJsonBody(request);
    const auto result = auth_service_.RegisterService(
        utils::ExtractRequiredString(json, "service_name"),
        utils::ExtractOptionalString(json, "service_description"),
        utils::ExtractRequiredString(json, "admin_email"),
        utils::ExtractOptionalString(json, "admin_full_name"),
        utils::ExtractRequiredString(json, "password")
    );

    userver::formats::json::ValueBuilder response;
    response["session_token"] = result.session_token;
    response["user"]["id"] = result.user.id;
    response["user"]["client_service_id"] = result.user.client_service_id;
    response["user"]["client_service_name"] = result.user.client_service_name;
    response["user"]["email"] = result.user.email;
    response["user"]["full_name"] = result.user.full_name;
    response["user"]["status"] = result.user.status;
    response["user"]["role_codes_csv"] = utils::JoinCommaSeparated(result.user.role_codes);

    request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
    return utils::JsonResponse(request, std::move(response));
}

}  // namespace ab_experiments::handlers
