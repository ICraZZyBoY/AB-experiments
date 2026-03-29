#include "api_keys_handler.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildApiKeyItem(const services::ApiKeyView& api_key) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = api_key.id;
    item["name"] = api_key.name;
    item["status"] = api_key.status;
    item["created_at"] = api_key.created_at;
    return item;
}

}  // namespace

ApiKeysHandler::ApiKeysHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      auth_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster(),
          services::GetSessionStore()
      ) {}

std::string ApiKeysHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        if (!utils::HasRole(session, "ADMIN") && !utils::HasRole(session, "DEVELOPER")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN or DEVELOPER can view API keys";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto api_keys = auth_service_.ListApiKeys(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(api_keys.size());
        for (const auto& api_key : api_keys) {
            items.PushBack(BuildApiKeyItem(api_key));
        }
        response["items"] = items.ExtractValue();

        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can create API keys";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto created_api_key = auth_service_.CreateApiKey(
            session.client_service_id,
            utils::ExtractRequiredString(json, "name")
        );

        userver::formats::json::ValueBuilder response;
        response["api_key"]["id"] = created_api_key.api_key.id;
        response["api_key"]["name"] = created_api_key.api_key.name;
        response["api_key"]["status"] = created_api_key.api_key.status;
        response["api_key"]["created_at"] = created_api_key.api_key.created_at;
        response["plain_api_key"] = created_api_key.plain_api_key;

        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kDelete) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can revoke API keys";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const std::string api_key_id_arg = request.GetArg("api_key_id");
        if (api_key_id_arg.empty()) {
            throw userver::server::handlers::ClientError(
                userver::server::handlers::ExternalBody{"api_key_id query parameter is required"}
            );
        }

        std::int64_t api_key_id = 0;
        try {
            api_key_id = std::stoll(api_key_id_arg);
        } catch (const std::exception&) {
            throw userver::server::handlers::ClientError(
                userver::server::handlers::ExternalBody{"api_key_id must be a valid integer"}
            );
        }

        const auto revoked_api_key = auth_service_.RevokeApiKey(session.client_service_id, api_key_id);

        userver::formats::json::ValueBuilder response;
        response["api_key"]["id"] = revoked_api_key.id;
        response["api_key"]["name"] = revoked_api_key.name;
        response["api_key"]["status"] = revoked_api_key.status;
        response["api_key"]["created_at"] = revoked_api_key.created_at;
        response["message"] = "API key revoked";
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{"Unsupported method for api keys endpoint"}
    );
}

}  // namespace ab_experiments::handlers
