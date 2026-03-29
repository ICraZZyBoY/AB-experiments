#include "platform_users_handler.hpp"

#include <cstddef>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildUserItem(const services::PlatformUserView& user) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = user.id;
    item["client_service_id"] = user.client_service_id;
    item["email"] = user.email;
    item["full_name"] = user.full_name;
    item["status"] = user.status;
    item["role_codes_csv"] = utils::JoinCommaSeparated(user.role_codes);
    return item;
}

}  // namespace

PlatformUsersHandler::PlatformUsersHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      auth_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster(),
          services::GetSessionStore()
      ) {}

std::string PlatformUsersHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto users = auth_service_.ListPlatformUsers(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(users.size());
        for (const auto& user : users) {
            items.PushBack(BuildUserItem(user));
        }
        response["items"] = items.ExtractValue();

        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can create platform users";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto created_user = auth_service_.CreatePlatformUser(
            session.client_service_id,
            utils::ExtractRequiredString(json, "email"),
            utils::ExtractOptionalString(json, "full_name"),
            utils::ExtractRequiredString(json, "password"),
            utils::ExtractOptionalString(json, "role_code", "DEVELOPER")
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildUserItem(created_user));
        response["count"] = 1;
        response["items"] = items.ExtractValue();

        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{
            "Unsupported method for platform users endpoint"
        }
    );
}

}  // namespace ab_experiments::handlers
