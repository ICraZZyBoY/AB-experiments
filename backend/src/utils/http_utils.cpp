#include "http_utils.hpp"

#include <algorithm>
#include <stdexcept>

#include <userver/formats/json/serialize.hpp>
#include <userver/http/common_headers.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/server/http/http_response.hpp>

namespace ab_experiments::utils {

namespace {

constexpr std::string_view kBearerPrefix = "Bearer ";

}  // namespace

userver::formats::json::Value ParseJsonBody(const userver::server::http::HttpRequest& request) {
    const auto& body = request.RequestBody();
    if (body.empty()) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{"Request body must not be empty"}
        );
    }

    try {
        return userver::formats::json::FromString(body);
    } catch (const std::exception&) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{"Request body must be valid JSON"}
        );
    }
}

std::string JsonResponse(
    const userver::server::http::HttpRequest& request,
    userver::formats::json::ValueBuilder&& response
) {
    auto& response_meta = request.GetHttpResponse();
    response_meta.SetContentType("application/json");
    return userver::formats::json::ToString(response.ExtractValue());
}

std::string ExtractRequiredString(
    const userver::formats::json::Value& json,
    std::string_view field_name
) {
    const auto value = json[std::string(field_name)].As<std::string>("");
    if (value.empty()) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{
                "Field '" + std::string(field_name) + "' must not be empty"
            }
        );
    }

    return value;
}

std::string ExtractOptionalString(
    const userver::formats::json::Value& json,
    std::string_view field_name,
    std::string_view default_value
) {
    return json[std::string(field_name)].As<std::string>(std::string(default_value));
}

std::int64_t ExtractRequiredInt64(
    const userver::formats::json::Value& json,
    std::string_view field_name
) {
    const auto value = json[std::string(field_name)].As<std::int64_t>(0);
    if (value <= 0) {
        throw userver::server::handlers::ClientError(
            userver::server::handlers::ExternalBody{
                "Field '" + std::string(field_name) + "' must be a positive integer"
            }
        );
    }

    return value;
}

services::SessionData RequireSession(
    const userver::server::http::HttpRequest& request,
    const services::SessionStore& session_store
) {
    const auto& auth_header = request.GetHeader(userver::http::headers::kAuthorization);
    if (auth_header.empty() || auth_header.rfind(kBearerPrefix, 0) != 0) {
        throw userver::server::handlers::Unauthorized(
            userver::server::handlers::ExternalBody{
                "Authorization header must use Bearer token"
            }
        );
    }

    const auto token = auth_header.substr(kBearerPrefix.size());
    const auto session = session_store.Find(token);
    if (!session) {
        throw userver::server::handlers::Unauthorized(
            userver::server::handlers::ExternalBody{"Session token is invalid or expired"}
        );
    }

    return *session;
}

bool HasRole(const services::SessionData& session, std::string_view role_code) {
    return std::find(session.role_codes.begin(), session.role_codes.end(), role_code) !=
           session.role_codes.end();
}

std::vector<std::string> SplitCommaSeparated(std::string_view value) {
    std::vector<std::string> parts;
    if (value.empty()) {
        return parts;
    }

    std::size_t start = 0;
    while (start <= value.size()) {
        const auto separator = value.find(',', start);
        const auto length = separator == std::string_view::npos ? value.size() - start
                                                                : separator - start;
        if (length > 0) {
            parts.emplace_back(value.substr(start, length));
        }

        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }

    return parts;
}

std::string JoinCommaSeparated(const std::vector<std::string>& values) {
    std::string result;

    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += ',';
        }
        result += values[index];
    }

    return result;
}

}  // namespace ab_experiments::utils
