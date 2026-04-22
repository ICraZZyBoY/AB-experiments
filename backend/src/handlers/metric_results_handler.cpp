#include "metric_results_handler.hpp"

#include <optional>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildMetricResultItem(const services::MetricResultView& item) {
    userver::formats::json::ValueBuilder value(userver::formats::common::Type::kObject);
    value["experiment_id"] = item.experiment_id;
    value["metric_id"] = item.metric_id;
    value["metric_code"] = item.metric_code;
    value["metric_name"] = item.metric_name;
    value["variant_id"] = item.variant_id;
    value["variant_key"] = item.variant_key;
    value["variant_name"] = item.variant_name;
    value["period_start"] = item.period_start;
    value["period_end"] = item.period_end;
    value["value"] = item.value;
    if (item.std_error) {
        value["std_error"] = *item.std_error;
    }
    if (item.ci_low) {
        value["ci_low"] = *item.ci_low;
    }
    if (item.ci_high) {
        value["ci_high"] = *item.ci_high;
    }
    if (item.p_value) {
        value["p_value"] = *item.p_value;
    }
    if (item.lift) {
        value["lift"] = *item.lift;
    }
    return value;
}

std::optional<std::int64_t> ExtractOptionalExperimentId(
    const userver::server::http::HttpRequest& request
) {
    const auto experiment_id = request.GetArg("experiment_id");
    if (experiment_id.empty()) {
        return std::nullopt;
    }
    return std::stoll(experiment_id);
}

}  // namespace

MetricResultsHandler::MetricResultsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      metric_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string MetricResultsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto items = metric_service_.ListMetricResults(
            session.client_service_id,
            ExtractOptionalExperimentId(request)
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder values(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(items.size());
        response["auto_calculation_enabled"] = true;
        for (const auto& item : items) {
            values.PushBack(BuildMetricResultItem(item));
        }
        response["items"] = values.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        const auto json = utils::ParseJsonBody(request);
        const auto items = metric_service_.RecalculateExperimentMetrics(
            session.client_service_id,
            utils::ExtractRequiredInt64(json, "experiment_id")
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder values(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(items.size());
        response["auto_calculation_enabled"] = true;
        for (const auto& item : items) {
            values.PushBack(BuildMetricResultItem(item));
        }
        response["items"] = values.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{
            "Unsupported method for metric results endpoint"
        }
    );
}

}  // namespace ab_experiments::handlers
