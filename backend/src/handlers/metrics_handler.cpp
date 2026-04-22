#include "metrics_handler.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildMetricItem(const services::MetricView& metric) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = metric.id;
    item["code"] = metric.code;
    item["name"] = metric.name;
    item["description"] = metric.description;
    item["metric_type"] = metric.metric_type;
    item["aggregation_unit"] = metric.aggregation_unit;
    item["feature_key"] = metric.feature_key;
    return item;
}

}  // namespace

MetricsHandler::MetricsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      metric_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string MetricsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto metrics = metric_service_.ListMetrics(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(metrics.size());
        for (const auto& metric : metrics) {
            items.PushBack(BuildMetricItem(metric));
        }
        response["items"] = items.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can create metrics";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto metric = metric_service_.CreateMetric(
            session.client_service_id,
            utils::ExtractRequiredString(json, "code"),
            utils::ExtractRequiredString(json, "name"),
            utils::ExtractOptionalString(json, "description"),
            utils::ExtractRequiredString(json, "metric_type"),
            utils::ExtractRequiredString(json, "feature_key")
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildMetricItem(metric));
        response["count"] = 1;
        response["items"] = items.ExtractValue();
        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{"Unsupported method for metrics endpoint"}
    );
}

}  // namespace ab_experiments::handlers
