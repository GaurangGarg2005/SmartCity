// config.cpp — Configuration loader from environment variables (Phase 0/2)
#include "config.h"
#include <cstdlib>
#include <stdexcept>

namespace smartcity {

static const char* env_or(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return v ? v : def;
}

static int env_int(const char* key, int def) {
    const char* v = std::getenv(key);
    return v ? std::atoi(v) : def;
}

Config Config::from_env() {
    Config cfg;
    cfg.port          = env_int("PORT", 8080);
    cfg.graph_path    = env_or("GRAPH_PATH",     "data/graph.bin");
    cfg.places_path   = env_or("PLACES_PATH",    "data/places.json");
    cfg.sim_vehicles  = env_int("SIM_VEHICLES",  500);
    cfg.sim_seed      = env_int("SIM_SEED",      42);
    cfg.weight_model  = env_or("WEIGHT_MODEL",   "greenshields");
    cfg.tomtom_key    = env_or("TOMTOM_API_KEY", "");
    cfg.cors_origin   = env_or("CORS_ORIGIN",    "*");
    return cfg;
}

} // namespace smartcity
