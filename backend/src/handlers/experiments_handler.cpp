#include "experiments_handler.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

[[noreturn]] void ThrowClientError(const std::string& message) {
    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{message}
    );
}

double ExtractRequiredPositiveDouble(
    const userver::formats::json::Value& json,
    std::string_view field_name
) {
    const auto value = json[std::string(field_name)].As<double>(0.0);
    if (value <= 0.0) {
        ThrowClientError(
            "Field '" + std::string(field_name) + "' must be a positive number"
        );
    }

    return value;
}

std::int32_t ExtractRequiredPositiveInt(
    const userver::formats::json::Value& json,
    std::string_view field_name
) {
    const auto value = json[std::string(field_name)].As<int>(0);
    if (value <= 0) {
        ThrowClientError(
            "Field '" + std::string(field_name) + "' must be a positive integer"
        );
    }

    return value;
}

services::ExperimentFlagInput ParseExperimentFlagInput(
    const userver::formats::json::Value& json
) {
    return services::ExperimentFlagInput{
        utils::ExtractRequiredInt64(json, "flag_id"),
        utils::ExtractRequiredString(json, "variant_value"),
    };
}

services::ExperimentVariantInput ParseExperimentVariantInput(
    const userver::formats::json::Value& json
) {
    const auto flags_json = json["flags"];
    if (flags_json.GetSize() == 0) {
        ThrowClientError("Every variant must contain at least one flag");
    }

    services::ExperimentVariantInput variant;
    variant.key = utils::ExtractRequiredString(json, "key");
    variant.name = utils::ExtractRequiredString(json, "name");
    variant.description = utils::ExtractOptionalString(json, "description");
    variant.flags.reserve(flags_json.GetSize());

    for (const auto& flag_json : flags_json) {
        variant.flags.push_back(ParseExperimentFlagInput(flag_json));
    }

    return variant;
}

services::CreateExperimentInput ParseCreateExperimentInput(
    const userver::formats::json::Value& json
) {
    const auto variants_json = json["variants"];
    if (variants_json.GetSize() == 0) {
        ThrowClientError("Field 'variants' must contain at least one item");
    }

    services::CreateExperimentInput input;
    input.name = utils::ExtractRequiredString(json, "name");
    input.description = utils::ExtractOptionalString(json, "description");
    input.layer_name = utils::ExtractRequiredString(json, "layer_name");
    input.layer_description = utils::ExtractOptionalString(json, "layer_description");
    input.duration_days = ExtractRequiredPositiveInt(json, "duration_days");
    input.variant_traffic_percent =
        ExtractRequiredPositiveDouble(json, "variant_traffic_percent");
    input.variants.reserve(variants_json.GetSize());

    for (const auto& variant_json : variants_json) {
        input.variants.push_back(ParseExperimentVariantInput(variant_json));
    }

    return input;
}

bool CanChangeExperimentLifecycle(const services::SessionData& session) {
    static_cast<void>(session);
    return services::ExperimentService::CanApproveExperiment();
}

userver::formats::json::ValueBuilder BuildExperimentFlagItem(
    const services::ExperimentFlagView& flag
) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["flag_id"] = flag.flag_id;
    item["flag_key"] = flag.flag_key;
    item["flag_name"] = flag.flag_name;
    item["flag_description"] = flag.flag_description;
    item["value_type"] = flag.value_type;
    item["default_value"] = flag.default_value;
    item["variant_value"] = flag.variant_value;
    return item;
}

userver::formats::json::ValueBuilder BuildExperimentVariantItem(
    const services::ExperimentVariantView& variant
) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    userver::formats::json::ValueBuilder flags(userver::formats::common::Type::kArray);

    item["id"] = variant.id;
    item["key"] = variant.key;
    item["name"] = variant.name;
    item["description"] = variant.description;
    item["traffic_weight"] = variant.traffic_weight;
    item["traffic_percent"] = variant.traffic_percent;

    for (const auto& flag : variant.flags) {
        flags.PushBack(BuildExperimentFlagItem(flag));
    }
    item["flags"] = flags.ExtractValue();

    return item;
}

userver::formats::json::ValueBuilder BuildExperimentItem(const services::ExperimentView& experiment) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    userver::formats::json::ValueBuilder variants(userver::formats::common::Type::kArray);

    item["id"] = experiment.id;
    item["client_service_id"] = experiment.client_service_id;
    item["experiment_layer_id"] = experiment.experiment_layer_id;
    item["layer_name"] = experiment.layer_name;
    item["name"] = experiment.name;
    item["description"] = experiment.description;
    item["status"] = experiment.status;
    item["salt"] = experiment.salt;
    item["duration_days"] = experiment.duration_days;
    item["start_at"] = experiment.start_at;
    item["end_at"] = experiment.end_at;
    item["created_at"] = experiment.created_at;
    item["variant_traffic_percent"] = experiment.variant_traffic_percent;
    item["total_traffic_percent"] = experiment.total_traffic_percent;
    item["metric_calculation_dirty"] = experiment.metric_calculation_dirty;
    item["metric_calculation_dirty_reason"] = experiment.metric_calculation_dirty_reason;
    item["metric_calculation_last_assignment_at"] =
        experiment.metric_calculation_last_assignment_at;
    item["metric_calculation_last_event_at"] = experiment.metric_calculation_last_event_at;
    item["metric_calculation_last_calculated_at"] =
        experiment.metric_calculation_last_calculated_at;

    for (const auto& variant : experiment.variants) {
        variants.PushBack(BuildExperimentVariantItem(variant));
    }
    item["variants"] = variants.ExtractValue();

    userver::formats::json::ValueBuilder available_actions(
        userver::formats::common::Type::kArray
    );
    for (const auto& action : experiment.available_actions) {
        available_actions.PushBack(action);
    }
    item["available_actions"] = available_actions.ExtractValue();

    return item;
}

}  // namespace

ExperimentsHandler::ExperimentsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      experiment_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string ExperimentsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto session = utils::RequireSession(request, services::GetSessionStore());

    if (request.GetMethod() == userver::server::http::HttpMethod::kGet) {
        const auto experiments = experiment_service_.ListExperiments(session.client_service_id);

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        response["count"] = static_cast<int>(experiments.size());
        for (const auto& experiment : experiments) {
            items.PushBack(BuildExperimentItem(experiment));
        }
        response["items"] = items.ExtractValue();

        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPost) {
        if (!utils::HasRole(session, "ADMIN")) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "Only ADMIN can create experiments";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto experiment = experiment_service_.CreateExperiment(
            session.client_service_id,
            session.user_id,
            ParseCreateExperimentInput(json)
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildExperimentItem(experiment));
        response["count"] = 1;
        response["items"] = items.ExtractValue();

        request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kCreated);
        return utils::JsonResponse(request, std::move(response));
    }

    if (request.GetMethod() == userver::server::http::HttpMethod::kPatch) {
        if (!CanChangeExperimentLifecycle(session)) {
            userver::formats::json::ValueBuilder response;
            response["message"] = "You are not allowed to change experiment lifecycle";
            request.GetHttpResponse().SetStatus(userver::server::http::HttpStatus::kForbidden);
            return utils::JsonResponse(request, std::move(response));
        }

        const auto json = utils::ParseJsonBody(request);
        const auto experiment = experiment_service_.UpdateExperimentStatus(
            session.client_service_id,
            utils::ExtractRequiredInt64(json, "experiment_id"),
            utils::ExtractRequiredString(json, "action")
        );

        userver::formats::json::ValueBuilder response;
        userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
        items.PushBack(BuildExperimentItem(experiment));
        response["count"] = 1;
        response["items"] = items.ExtractValue();
        return utils::JsonResponse(request, std::move(response));
    }

    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{
            "Unsupported method for experiments endpoint"
        }
    );
}

}  // namespace ab_experiments::handlers
