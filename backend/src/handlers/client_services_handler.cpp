#include "client_services_handler.hpp"

#include <cstddef>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/storages/postgres/component.hpp>

namespace ab_experiments::handlers {

namespace {

userver::formats::json::ValueBuilder BuildClientServiceItem(
    const repositories::ClientServiceRecord& client_service
) {
    userver::formats::json::ValueBuilder item(userver::formats::common::Type::kObject);
    item["id"] = client_service.id;
    item["name"] = client_service.name;
    item["description"] = client_service.description;
    item["status"] = client_service.status;
    return item;
}

}  // namespace

ClientServicesHandler::ClientServicesHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context
)
    : HttpHandlerBase(config, context),
      client_service_repository_(
          context.FindComponent<userver::components::Postgres>("postgres-db").GetCluster()
      ) {}

std::string ClientServicesHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    const auto client_services = client_service_repository_.ListClientServices();

    userver::formats::json::ValueBuilder response;
    userver::formats::json::ValueBuilder items(userver::formats::common::Type::kArray);
    response["count"] = static_cast<int>(client_services.size());

    for (const auto& client_service : client_services) {
        items.PushBack(BuildClientServiceItem(client_service));
    }
    response["items"] = items.ExtractValue();

    auto& response_meta = request.GetHttpResponse();
    response_meta.SetContentType("application/json");

    return userver::formats::json::ToString(response.ExtractValue());
}

}  // namespace ab_experiments::handlers
