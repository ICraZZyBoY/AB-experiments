#include "experiment_metrics_handler.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildExperimentMetricItem(
    const services::ExperimentMetricView& item
) {
    userver::formats::json::ValueBuilder value(userver::formats::common::Type::kObject);
    value["experiment_id"] = item.experiment_id;
    value["metric_id"] = item.metric_id;
    value["metric_code"] = item.metric_code;
    value["metric_name"] = item.metric_name;
    value["metric_type"] = item.metric_type;
    value["aggregation_unit"] = item.aggregation_unit;
    value["feature_key"] = item.feature_key;
    value["is_primary"] = item.is_primary;
    value["is_guardrail"] = item.is_guardrail;
    return value;
}

}  // namespace

ExperimentMetricsHandler::ExperimentMetricsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      metric_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string ExperimentMetricsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto items = metric_service_.ListExperimentMetrics(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder values(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(items.size());
        for (const auto& item : items) {
            values.PushBack(BuildExperimentMetricItem(item));
        }
        response["items"] = values.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can attach metrics to experiments";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto item = metric_service_.AttachMetricToExperiment(
            session.client_service_id,
            utils::ExtractRequiredInt64(json, "experiment_id"),
            utils::ExtractRequiredInt64(json, "metric_id"),
            json["is_primary"].As<bool>(false),
            json["is_guardrail"].As<bool>(false)
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder values(userver::formats::common::Type::kArray);
        values.PushBack(BuildExperimentMetricItem(item));
        response["count"] = 1;
        response["items"] = values.ExtractValue();
        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{
            "Unsupported method for experiment metrics endpoint"
        }
    );
}

}  // namespace ab_experiments::handlers
