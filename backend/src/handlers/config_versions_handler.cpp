#include "config_versions_handler.hpp"

#include <cstddef>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/formats/json.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildConfigVersionItem(
    const services::ConfigVersionView& config_version
) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = config_version.id;
    item["client_service_id"] = config_version.client_service_id;
    item["version"] = config_version.version;
    item["created_at"] = config_version.created_at;
    item["experiment_count"] = config_version.experiment_count;
    item["config_json"] = userver::formats::json::FromString(config_version.config_json);
    return item;
}

}  // namespace

ConfigVersionsHandler::ConfigVersionsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      config_version_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string ConfigVersionsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto versions = config_version_service_.ListConfigVersions(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(versions.size());
        response["auto_build_enabled"] = true;

        for (const auto& version : versions) {
            items.PushBack(BuildConfigVersionItem(version));
        }

        response["items"] = items.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can build config versions";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto built = config_version_service_.BuildNextVersion(
            session.client_service_id,
            session.user_id
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildConfigVersionItem(built.config_version));
        response["count"] = 1;
        response["auto_build_enabled"] = true;
        response["queued_added_count"] = built.queued_added_count;
        response["completed_removed_count"] = built.completed_removed_count;
        response["items"] = items.ExtractValue();

        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{
            "Unsupported method for config versions endpoint"
        }
    );
}

}  // namespace ab_experiments::handlers
