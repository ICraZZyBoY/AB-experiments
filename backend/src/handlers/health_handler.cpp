#include "health_handler.hpp"

#include <userver/server/http/http_response.hpp>

namespace ab_experiments::handlers {

std::string HealthHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context
) const {
    static_cast<void>(context);

    auto& response = request.GetHttpResponse();
    response.SetContentType("application/json");

    return R"({"status":"ok","service":"ab-experiments-backend","framework":"userver"})";
}

}  // namespace ab_experiments::handlers
