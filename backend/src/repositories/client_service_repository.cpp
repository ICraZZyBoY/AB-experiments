#include "client_service_repository.hpp"

#include <utility>

namespace ab_experiments::repositories {

namespace postgres = userver::storages::postgres;

ClientServiceRepository::ClientServiceRepository(postgres::ClusterPtr pg_cluster)
    : pg_cluster_(std::move(pg_cluster)) {}

std::vector<ClientServiceRecord> ClientServiceRepository::ListClientServices() const {
    const auto result = pg_cluster_->Execute(
        postgres::ClusterHostType::kMaster,
        "SELECT id, name, COALESCE(description, '') AS description, status "
        "FROM abtest.ClientService "
        "ORDER BY id"
    );

    std::vector<ClientServiceRecord> client_services;
    client_services.reserve(result.Size());

    for (const auto& row : result) {
        ClientServiceRecord client_service{
            row["id"].As<std::int64_t>(),
            row["name"].As<std::string>(),
            row["description"].As<std::string>(),
            row["status"].As<std::string>(),
        };
        client_services.push_back(std::move(client_service));
    }

    return client_services;
}

}  // namespace ab_experiments::repositories
