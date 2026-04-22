#include "runtime_events_handler.hpp"

#include <optional>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/component.hpp>

#include "services/session_store.hpp"
#include "utils/http_utils.hpp"

namespace ab_experiments::handlers {

namespace {

constexpr std::string_view kApiKeyHeader = "X-API-Key";

std::optional<double> ExtractOptionalDouble(const userver::formats::json::Value& json, std::string_view key) {
    if (!json.HasMember(key)) {
        return std::nullopt;
    }
    return json[std::string(key)].As<double>();
}

services::EventInput ParseInput(const userver::formats::json::Value& json) {
    services::EventInput input;
    input.feature_key = utils::ExtractRequiredString(json, "feature_key");
    input.req_id = utils::ExtractOptionalString(json, "req_id");
    input.external_user_id = utils::ExtractOptionalString(json, "external_user_id");
    input.device_id = utils::ExtractOptionalString(json, "device_id");
    input.ip_address = utils::ExtractOptionalString(json, "ip_address");
    input.occurred_at = utils::ExtractOptionalString(json, "occurred_at");
    input.value = ExtractOptionalDouble(json, "value");
    if (json.HasMember("properties")) {
        input.properties_json = userver::formats::json::ToString(json["properties"]);
    }
    return input;
}

}  // namespace

RuntimeEventsHandler::RuntimeEventsHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      auth_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster(),
          services::GetSessionStore()
      ),
      event_service_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string RuntimeEventsHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    if (request.GetMethod() != userver::server::http::HttpMethod::kPost) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{
                "Unsupported method for runtime events endpoint"
            }
        );
    }

    const auto api_key = auth_service_.AuthenticateApiKey(request.GetHeader(kApiKeyHeader));
    const auto result = event_service_.IngestEvent(
        api_key.client_service_id,
        ParseInput(utils::ParseJsonBody(request))
    );

    userver::formats::json::ValueBuilder response;
    response["event_id"] = result.event_id;
    response["client_service_id"] = api_key.client_service_id;
    response["api_key_name"] = api_key.name;
    response["end_user_id"] = result.end_user_id;
    response["feature_id"] = result.feature_id;
    response["feature_key"] = result.feature_key;
    response["occurred_at"] = result.occurred_at;
    return utils::JsonResponse(request, std::move(response));
}

}  // namespace ab_experiments::handlers
