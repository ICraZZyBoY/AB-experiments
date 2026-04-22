#include "runtime_flags_handler.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

constexpr std::string_view kApiKeyHeader = "X-API-Key";

userver::formats::json::ValueBuilder BuildAssignmentItem(
    const services::AssignmentResultItem& assignment
) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["experiment_id"] = assignment.experiment_id;
    item["experiment_name"] = assignment.experiment_name;
    item["layer_name"] = assignment.layer_name;
    item["variant_id"] = assignment.variant_id;
    item["variant_key"] = assignment.variant_key;
    item["variant_name"] = assignment.variant_name;
    return item;
}

userver::formats::json::ValueBuilder BuildFlagItem(const services::AssignedFlagValue& flag) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["flag_id"] = flag.flag_id;
    item["flag_key"] = flag.flag_key;
    item["flag_name"] = flag.flag_name;
    item["value_type"] = flag.value_type;
    item["default_value"] = flag.default_value;
    item["value"] = flag.value;
    item["experiment_id"] = flag.experiment_id;
    item["variant_id"] = flag.variant_id;
    item["layer_name"] = flag.layer_name;
    return item;
}

services::AssignmentRequestInput ParseInput(const userver::formats::json::Value& json) {
    return services::AssignmentRequestInput{
        utils::ExtractRequiredString(json, "req_id"),
        utils::ExtractOptionalString(json, "external_user_id"),
        utils::ExtractOptionalString(json, "device_id"),
        utils::ExtractOptionalString(json, "ip_address"),
        utils::ExtractOptionalString(json, "test_id"),
    };
}

}  // namespace

RuntimeFlagsHandler::RuntimeFlagsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      auth_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster(),
          services::GetSessionStore()
      ),
      assignment_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string RuntimeFlagsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    if (request.GetMethod() != userver::server::http::HttpMethod::kPost) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{
                "Unsupported method for runtime flags endpoint"
            }
        );
    }

    const auto api_key = auth_service_.AuthenticateApiKey(request.GetHeader(kApiKeyHeader));
    const auto input = ParseInput(utils::ParseJsonBody(request));
    const auto result = assignment_service_.ResolveFlags(api_key.client_service_id, input);

    userver::formats::json::ValueBuilder response;
    userver::formats::json::ValueBuilder assignments(userver::formats::common::Type::kArray);
    userver::formats::json::ValueBuilder flags(userver::formats::common::Type::kArray);
    userver::formats::json::ValueBuilder flag_values(userver::formats::common::Type::kObject);

    response["client_service_id"] = api_key.client_service_id;
    response["config_version"] = result.config_version;
    response["req_id"] = input.req_id;
    response["test_id"] = input.test_id;
    response["end_user_id"] = result.end_user_id;
    response["api_key_name"] = api_key.name;
    response["assignments_count"] = static_cast<int>(result.assignments.size());
    response["flags_count"] = static_cast<int>(result.flags.size());

    for (const auto& assignment : result.assignments) {
        assignments.PushBack(BuildAssignmentItem(assignment));
    }

    for (const auto& flag : result.flags) {
        flags.PushBack(BuildFlagItem(flag));
        flag_values[flag.flag_key] = flag.value;
    }

    response["assignments"] = assignments.ExtractValue();
    response["flags"] = flags.ExtractValue();
    response["flag_values"] = flag_values.ExtractValue();
    return utils::JsonResponse(request, std::move(response));
}

}  // namespace ab_experiments::handlers
