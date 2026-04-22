#include "flags_handler.hpp"

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

userver::formats::json::ValueBuilder BuildFlagItem(const services::FlagView& flag) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = flag.id;
    item["key"] = flag.key;
    item["name"] = flag.name;
    item["description"] = flag.description;
    item["value_type"] = flag.value_type;
    item["default_value"] = flag.default_value;
    return item;
}

}  // namespace

FlagsHandler::FlagsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      experiment_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string FlagsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto flags = experiment_service_.ListFlags(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(flags.size());
        for (const auto& flag : flags) {
            items.PushBack(BuildFlagItem(flag));
        }
        response["items"] = items.ExtractValue();

        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can create flags";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto created_flag = experiment_service_.CreateFlag(
            session.client_service_id,
            utils::ExtractRequiredString(json, "key"),
            utils::ExtractRequiredString(json, "name"),
            utils::ExtractOptionalString(json, "description"),
            utils::ExtractRequiredString(json, "value_type"),
            utils::ExtractOptionalString(json, "default_value")
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildFlagItem(created_flag));
        response["count"] = 1;
        response["items"] = items.ExtractValue();

        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{"Unsupported method for flags endpoint"}
    );
}

}  // namespace ab_experiments::handlers
