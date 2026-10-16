#pragma once
// config.h — Application configuration read from environment variables.
#include <string>

namespace smartcity {

struct Config {
    int         port;
    std::string graph_path;
    std::string places_path;
    int         sim_vehicles;
    int         sim_seed;
    std::string weight_model;   // "greenshields" or "bpr"
    std::string tomtom_key;     // empty = disabled
    std::string cors_origin;

    static Config from_env();
};

} // namespace smartcity
