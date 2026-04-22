#include "features_handler.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildFeatureItem(const services::FeatureView& feature) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = feature.id;
    item["key"] = feature.key;
    item["name"] = feature.name;
    item["description"] = feature.description;
    return item;
}

}  // namespace

FeaturesHandler::FeaturesHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      event_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string FeaturesHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto features = event_service_.ListFeatures(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(features.size());
        for (const auto& feature : features) {
            items.PushBack(BuildFeatureItem(feature));
        }
        response["items"] = items.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can create features";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto created_feature = event_service_.CreateFeature(
            session.client_service_id,
            utils::ExtractRequiredString(json, "key"),
            utils::ExtractRequiredString(json, "name"),
            utils::ExtractOptionalString(json, "description")
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildFeatureItem(created_feature));
        response["count"] = 1;
        response["items"] = items.ExtractValue();

        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{"Unsupported method for features endpoint"}
    );
}

}  // namespace ab_experiments::handlers
