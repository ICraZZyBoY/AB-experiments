#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>

#include "services/session_store.hpp"

namespace ab_experiments::utils {

userver::formats::json::Value ParseJsonBody(const userver::server::http::HttpRequest& request);

std::string JsonResponse(
    const userver::server::http::HttpRequest& request,
    userver::formats::json::ValueBuilder&& response
);

std::string ExtractRequiredString(
    const userver::formats::json::Value& json,
    std::string_view field_name
);

std::string ExtractOptionalString(
    const userver::formats::json::Value& json,
    std::string_view field_name,
    std::string_view default_value = {}
);

std::int64_t ExtractRequiredInt64(
    const userver::formats::json::Value& json,
    std::string_view field_name
);

services::SessionData RequireSession(
    const userver::server::http::HttpRequest& request,
    const services::SessionStore& session_store
);

bool HasRole(const services::SessionData& session, std::string_view role_code);

std::vector<std::string> SplitCommaSeparated(std::string_view value);

std::string JoinCommaSeparated(const std::vector<std::string>& values);

}  // namespace ab_experiments::utils
