#include "login_handler.hpp"

#include <optional>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

LoginHandler::LoginHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      auth_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster(),
          services::GetSessionStore()
      ) {}

std::string LoginHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto json = utils::ParseJsonBody(request);
    const auto result = auth_service_.Login(
        utils::ExtractRequiredString(json, "email"),
        utils::ExtractRequiredString(json, "password"),
        json["client_service_id"].As<std::int64_t>(0) > 0
            ? std::optional<std::int64_t>(json["client_service_id"].As<std::int64_t>(0))
            : std::nullopt
    );

    userver::formats::json::ValueBuilder response;
    if (!result.auth_response) {
        userver::formats::json::ValueBuilder services(
            userver::formats::common::Type::kArray
        );
        response["requires_service_selection"] = true;

        for (const auto& service_option : result.service_options) {
            userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
            item["client_service_id"] = service_option.client_service_id;
            item["client_service_name"] = service_option.client_service_name;
            services.PushBack(std::move(item));
        }

        response["service_options"] = services.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    response["requires_service_selection"] = false;
    response["session_token"] = result.auth_response->session_token;
    response["user"]["id"] = result.auth_response->user.id;
    response["user"]["client_service_id"] = result.auth_response->user.client_service_id;
    response["user"]["client_service_name"] = result.auth_response->user.client_service_name;
    response["user"]["email"] = result.auth_response->user.email;
    response["user"]["full_name"] = result.auth_response->user.full_name;
    response["user"]["status"] = result.auth_response->user.status;
    response["user"]["role_codes_csv"] =
        utils::JoinCommaSeparated(result.auth_response->user.role_codes);

    return utils::JsonResponse(request, std::move(response));
}

}  // namespace ab_experiments::handlers
