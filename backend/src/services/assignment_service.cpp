#include "assignment_service.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <userver/crypto/hash.hpp>
#include <userver/formats/json.hpp>
#include <userver/server/handlers/exceptions.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "metric_calculation_state.hpp"

namespace ab_experiments::services {

namespace postgres = userver::storages::postgres;

namespace {

struct FlagSnapshot {
    std::int64_t flag_id{0};
    std::string flag_key;
    std::string flag_name;
    std::string value_type;
    std::string default_value;
    std::string variant_value;
};

struct VariantSnapshot {
    std::int64_t variant_id{0};
    std::string key;
    std::string name;
    double traffic_weight{0.0};
    std::vector<FlagSnapshot> flags;
};

struct ExperimentSnapshot {
    std::int64_t experiment_id{0};
    std::string name;
    std::string layer_name;
    std::string salt;
    double total_traffic{0.0};
    std::vector<VariantSnapshot> variants;
};

struct LayerSnapshot {
    std::string layer_name;
    std::vector<ExperimentSnapshot> experiments;
};

[[noreturn]] void ThrowClientError(const std::string& message) {
    throw userver::server::handlers::ClientError(
        userver::server::handlers::ExternalBody{message}
    );
}

double ClampTraffic(double value) {
    return std::max(0.0, std::min(1.0, value));
}

std::uint64_t ParseHexPrefix(std::string_view value, std::size_t digits) {
    std::uint64_t result = 0;
    const auto limit = std::min(digits, value.size());
    for (std::size_t index = 0; index < limit; ++index) {
        const auto ch = value[index];
        result <<= 4U;
        if (ch >= '0' && ch <= '9') {
            result |= static_cast<std::uint64_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            result |= static_cast<std::uint64_t>(10 + ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            result |= static_cast<std::uint64_t>(10 + ch - 'A');
        }
    }
    return result;
}

double HashToUnitInterval(std::string_view input) {
    const auto digest = userver::crypto::hash::Sha256(input);
    const auto raw = ParseHexPrefix(digest, 16);
    constexpr long double kMaxUint64 =
        static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    return static_cast<double>(static_cast<long double>(raw) / (kMaxUint64 + 1.0L));
}

FlagSnapshot ParseFlagSnapshot(const userver::formats::json::Value& json) {
    return FlagSnapshot{
        json["flag_id"].As<std::int64_t>(0),
        json["flag_key"].As<std::string>(""),
        json["flag_name"].As<std::string>(""),
        json["value_type"].As<std::string>(""),
        json["default_value"].As<std::string>(""),
        json["variant_value"].As<std::string>(""),
    };
}

VariantSnapshot ParseVariantSnapshot(const userver::formats::json::Value& json) {
    VariantSnapshot variant;
    variant.variant_id = json["variant_id"].As<std::int64_t>(0);
    variant.key = json["key"].As<std::string>("");
    variant.name = json["name"].As<std::string>("");
    variant.traffic_weight = ClampTraffic(json["traffic_weight"].As<double>(0.0));

    const auto flags_json = json["flags"];
    variant.flags.reserve(flags_json.GetSize());
    for (const auto& flag_json : flags_json) {
        variant.flags.push_back(ParseFlagSnapshot(flag_json));
    }

    return variant;
}

ExperimentSnapshot ParseExperimentSnapshot(const userver::formats::json::Value& json) {
    ExperimentSnapshot experiment;
    experiment.experiment_id = json["experiment_id"].As<std::int64_t>(0);
    experiment.name = json["name"].As<std::string>("");
    experiment.layer_name = json["layer_name"].As<std::string>("");
    experiment.salt = json["salt"].As<std::string>("");

    const auto variants_json = json["variants"];
    experiment.variants.reserve(variants_json.GetSize());
    for (const auto& variant_json : variants_json) {
        auto variant = ParseVariantSnapshot(variant_json);
        experiment.total_traffic += variant.traffic_weight;
        experiment.variants.push_back(std::move(variant));
    }
    experiment.total_traffic = ClampTraffic(experiment.total_traffic);

    return experiment;
}

std::vector<LayerSnapshot> ParseLayers(const userver::formats::json::Value& config_json) {
    std::vector<LayerSnapshot> layers;
    const auto experiments_json = config_json["experiments"];

    for (const auto& experiment_json : experiments_json) {
        auto experiment = ParseExperimentSnapshot(experiment_json);
        auto layer_it = std::find_if(
            layers.begin(),
            layers.end(),
            [&experiment](const LayerSnapshot& layer) {
                return layer.layer_name == experiment.layer_name;
            }
        );
        if (layer_it == layers.end()) {
            layers.push_back(LayerSnapshot{experiment.layer_name, {}});
            layer_it = std::prev(layers.end());
        }
        layer_it->experiments.push_back(std::move(experiment));
    }

    return layers;
}

std::string GetStickySubjectKey(const AssignmentRequestInput& input) {
    if (!input.external_user_id.empty()) {
        return input.external_user_id;
    }
    if (!input.device_id.empty()) {
        return input.device_id;
    }

    ThrowClientError("Either 'external_user_id' or 'device_id' must be provided");
}

std::int64_t EnsureEndUser(
    postgres::Transaction& transaction,
    std::int64_t client_service_id,
    const AssignmentRequestInput& input
) {
    if (!input.device_id.empty()) {
        const auto lookup_result = transaction.Execute(
            "SELECT id "
            "FROM abtest.EndUser "
            "WHERE client_service_id = $1 AND device_id = $2 "
            "ORDER BY id "
            "LIMIT 1",
            client_service_id,
            input.device_id
        );

        if (!lookup_result.IsEmpty()) {
            const auto end_user_id = lookup_result[0]["id"].As<std::int64_t>();
            transaction.Execute(
                "UPDATE abtest.EndUser "
                "SET external_user_id = COALESCE(NULLIF($2, ''), external_user_id), "
                "    device_id = COALESCE(NULLIF($3, ''), device_id), "
                "    ip_address = COALESCE(NULLIF($4, '')::inet, ip_address) "
                "WHERE id = $1",
                end_user_id,
                input.external_user_id,
                input.device_id,
                input.ip_address
            );
            return end_user_id;
        }
    } else {
        const auto lookup_result = transaction.Execute(
            "SELECT id "
            "FROM abtest.EndUser "
            "WHERE client_service_id = $1 AND external_user_id = $2 "
            "ORDER BY id "
            "LIMIT 1",
            client_service_id,
            input.external_user_id
        );

        if (!lookup_result.IsEmpty()) {
            const auto end_user_id = lookup_result[0]["id"].As<std::int64_t>();
            transaction.Execute(
                "UPDATE abtest.EndUser "
                "SET external_user_id = COALESCE(NULLIF($2, ''), external_user_id), "
                "    device_id = COALESCE(NULLIF($3, ''), device_id), "
                "    ip_address = COALESCE(NULLIF($4, '')::inet, ip_address) "
                "WHERE id = $1",
                end_user_id,
                input.external_user_id,
                input.device_id,
                input.ip_address
            );
            return end_user_id;
        }
    }

    const auto insert_result = transaction.Execute(
        "INSERT INTO abtest.EndUser(client_service_id, external_user_id, device_id, ip_address) "
        "VALUES ($1, NULLIF($2, ''), NULLIF($3, ''), NULLIF($4, '')::inet) "
        "RETURNING id",
        client_service_id,
        input.external_user_id,
        input.device_id,
        input.ip_address
    );
    return insert_result[0]["id"].As<std::int64_t>();
}

const ExperimentSnapshot* SelectExperiment(
    const LayerSnapshot& layer,
    const std::string& subject_key
) {
    const auto layer_bucket =
        HashToUnitInterval(subject_key + ":layer:" + layer.layer_name);

    double cursor = 0.0;
    for (const auto& experiment : layer.experiments) {
        const auto width = ClampTraffic(experiment.total_traffic);
        if (width <= 0.0) {
            continue;
        }

        const auto next_cursor = std::min(1.0, cursor + width);
        if (layer_bucket >= cursor && layer_bucket < next_cursor) {
            return &experiment;
        }
        cursor = next_cursor;
        if (cursor >= 1.0) {
            break;
        }
    }

    return nullptr;
}

const VariantSnapshot* FindVariantByTestId(
    const ExperimentSnapshot& experiment,
    std::string_view test_id
) {
    if (test_id.empty()) {
        return nullptr;
    }

    return [&experiment, test_id]() -> const VariantSnapshot* {
        for (const auto& variant : experiment.variants) {
            if (std::to_string(variant.variant_id) == test_id || variant.key == test_id) {
                return &variant;
            }
        }
        return nullptr;
    }();
}

const ExperimentSnapshot* SelectExperimentByTestId(
    const LayerSnapshot& layer,
    std::string_view test_id,
    const VariantSnapshot** forced_variant
) {
    if (forced_variant) {
        *forced_variant = nullptr;
    }
    if (test_id.empty()) {
        return nullptr;
    }

    for (const auto& experiment : layer.experiments) {
        const auto* variant = FindVariantByTestId(experiment, test_id);
        if (!variant) {
            continue;
        }

        if (forced_variant) {
            *forced_variant = variant;
        }
        return &experiment;
    }

    return nullptr;
}

const VariantSnapshot* SelectVariant(
    const ExperimentSnapshot& experiment,
    const std::string& subject_key
) {
    if (experiment.variants.empty() || experiment.total_traffic <= 0.0) {
        return nullptr;
    }

    const auto variant_bucket =
        HashToUnitInterval(subject_key + ":experiment:" + experiment.salt) *
        experiment.total_traffic;

    double cursor = 0.0;
    for (const auto& variant : experiment.variants) {
        cursor += ClampTraffic(variant.traffic_weight);
        if (variant_bucket < cursor) {
            return &variant;
        }
    }

    return &experiment.variants.back();
}

}  // namespace

AssignmentService::AssignmentService(postgres::ClusterPtr pg_cluster)
    : pg_cluster_(std::move(pg_cluster)) {}

RuntimeFlagsResult AssignmentService::ResolveFlags(
    std::int64_t client_service_id,
    const AssignmentRequestInput& input
) const {
    EnsureMetricCalculationStateSchema(pg_cluster_);

    if (input.req_id.empty()) {
        ThrowClientError("Field 'req_id' must not be empty");
    }

    const auto subject_key = GetStickySubjectKey(input);

    auto transaction =
        pg_cluster_->Begin("resolve_runtime_flags", postgres::ClusterHostType::kMaster, {});

    RuntimeFlagsResult result;
    result.end_user_id = EnsureEndUser(transaction, client_service_id, input);

    const auto config_result = transaction.Execute(
        "SELECT version, config_json::text AS config_json "
        "FROM abtest.ConfigVersion "
        "WHERE client_service_id = $1 "
        "ORDER BY version DESC "
        "LIMIT 1",
        client_service_id
    );

    if (config_result.IsEmpty()) {
        transaction.Commit();
        return result;
    }

    result.config_version = config_result[0]["version"].As<std::int32_t>();
    const auto config_json = userver::formats::json::FromString(
        config_result[0]["config_json"].As<std::string>()
    );
    const auto layers = ParseLayers(config_json);

    for (const auto& layer : layers) {
        const VariantSnapshot* forced_variant = nullptr;
        const auto* experiment = SelectExperimentByTestId(
            layer,
            input.test_id,
            &forced_variant
        );
        if (!experiment) {
            experiment = SelectExperiment(layer, subject_key);
        }
        if (!experiment) {
            continue;
        }

        const auto* variant = forced_variant ? forced_variant
                                             : SelectVariant(*experiment, subject_key);
        if (!variant) {
            continue;
        }

        result.assignments.push_back(AssignmentResultItem{
            experiment->experiment_id,
            experiment->name,
            experiment->layer_name,
            variant->variant_id,
            variant->key,
            variant->name,
        });

        transaction.Execute(
            "INSERT INTO abtest.AssignmentLog("
            "client_service_id, end_user_id, experiment_id, experiment_variant_id, req_id"
            ") VALUES ($1, $2, $3, $4, $5)",
            client_service_id,
            result.end_user_id,
            experiment->experiment_id,
            variant->variant_id,
            input.req_id
        );
        MarkExperimentMetricsDirty(
            transaction,
            client_service_id,
            experiment->experiment_id,
            kDirtyReasonAssignmentCreated
        );

        for (const auto& flag : variant->flags) {
            result.flags.push_back(AssignedFlagValue{
                flag.flag_id,
                flag.flag_key,
                flag.flag_name,
                flag.value_type,
                flag.default_value,
                flag.variant_value,
                experiment->experiment_id,
                variant->variant_id,
                experiment->layer_name,
            });
        }
    }

    transaction.Commit();
    return result;
}

}  // namespace ab_experiments::services
