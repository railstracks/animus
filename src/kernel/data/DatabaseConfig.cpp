#include "animus_kernel/DatabaseConfig.h"
#include "animus_kernel/KernelConfig.h"

#include <fstream>
#include <iostream>
#include <filesystem>

#include <json/json.h>

namespace animus::kernel {

bool DatabaseConfig::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;  // File not found — use defaults
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        std::cerr << "[db-config] failed to parse " << path << ": " << errors << std::endl;
        return false;
    }

    if (root.isMember("backend")) {
        backend = root["backend"].asString();
    }
    if (root.isMember("sqlite")) {
        const auto& sq = root["sqlite"];
        if (sq.isMember("path")) {
            sqlitePath = sq["path"].asString();
        }
    }
    if (root.isMember("postgresql")) {
        const auto& pg = root["postgresql"];
        if (pg.isMember("host"))      pgHost = pg["host"].asString();
        if (pg.isMember("port"))      pgPort = pg["port"].asInt();
        if (pg.isMember("database"))  pgDatabase = pg["database"].asString();
        if (pg.isMember("username"))  pgUsername = pg["username"].asString();
        if (pg.isMember("password"))  pgPassword = pg["password"].asString();
        if (pg.isMember("pool_size")) pgPoolSize = pg["pool_size"].asInt();
    }

    std::cerr << "[db-config] loaded " << path
              << " (backend=" << backend << ")" << std::endl;
    return true;
}

void DatabaseConfig::ApplyTo(KernelConfig& config) const {
    // Only apply if the config still has defaults — CLI args take precedence.
    // We detect "unset" by checking if the value still matches the KernelConfig default.
    // For backend: default is "sqlite"
    if (config.storage_backend == "sqlite" && backend != "sqlite") {
        config.storage_backend = backend;
    }
    if (config.sqlite_path == "state/memory.db" && sqlitePath != "state/memory.db") {
        config.sqlite_path = sqlitePath;
    }
    if (config.pg_host == "localhost" && pgHost != "localhost") {
        config.pg_host = pgHost;
    }
    if (config.pg_port == 5432 && pgPort != 5432) {
        config.pg_port = pgPort;
    }
    if (config.pg_database == "animus" && pgDatabase != "animus") {
        config.pg_database = pgDatabase;
    }
    if (config.pg_username == "animus" && pgUsername != "animus") {
        config.pg_username = pgUsername;
    }
    if (config.pg_password.empty() && !pgPassword.empty()) {
        config.pg_password = pgPassword;
    }
    if (config.pg_pool_size == 10 && pgPoolSize != 10) {
        config.pg_pool_size = pgPoolSize;
    }
}

} // namespace animus::kernel