#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>

namespace ab_experiments::repositories {

struct ClientServiceRecord {
    std::int64_t id;
    std::string name;
    std::string description;
    std::string status;
};

class ClientServiceRepository final {
public:
    explicit ClientServiceRepository(userver::storages::postgres::ClusterPtr pg_cluster);

    std::vector<ClientServiceRecord> ListClientServices() const;

private:
    userver::storages::postgres::ClusterPtr pg_cluster_;
};

}  // namespace ab_experiments::repositories
