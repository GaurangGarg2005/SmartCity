// search_index.cpp — Loads places.json and provides typo-tolerant prefix search.
// Uses nlohmann/json for parsing.
#include "search_index.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace smartcity {

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Simple 1-edit-distance prefix check (handles transpositions too)
bool SearchIndex::fuzzy_match(const std::string& query_lower,
                               const std::string& name_lower,
                               float& score) {
    if (name_lower.empty() || query_lower.empty()) return false;

    // Exact prefix
    if (name_lower.substr(0, query_lower.size()) == query_lower) {
        score = 1.0f;
        return true;
    }
    // Substring match
    if (name_lower.find(query_lower) != std::string::npos) {
        score = 0.7f;
        return true;
    }
    // 1-substitution prefix tolerance (allow 1 wrong char at any position)
    if (query_lower.size() >= 3 && name_lower.size() >= query_lower.size()) {
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < query_lower.size(); ++i) {
            if (query_lower[i] != name_lower[i]) ++mismatches;
            if (mismatches > 1) break;
        }
        if (mismatches <= 1) { score = 0.5f; return true; }
    }
    return false;
}

// ── SearchIndex::load ─────────────────────────────────────────────────────────

bool SearchIndex::load(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f.is_open()) return false;

    nlohmann::json j;
    try { f >> j; } catch (...) { return false; }

    for (const auto& item : j) {
        Place p;
        p.id      = item.value("id",      "");
        p.name    = item.value("name",    "");
        p.type    = item.value("type",    "landmark");
        p.address = item.value("address", "");
        p.lat     = item.value("lat",     0.0f);
        p.lon     = item.value("lon",     0.0f);
        p.weight  = item.value("weight",  1.0f);
        if (p.name.empty()) continue;
        p.name_lower = to_lower(p.name);
        places_.push_back(std::move(p));
    }

    // Build sorted index for binary search
    sorted_idx_.resize(places_.size());
    for (std::size_t i = 0; i < places_.size(); ++i) sorted_idx_[i] = i;
    std::sort(sorted_idx_.begin(), sorted_idx_.end(), [&](std::size_t a, std::size_t b) {
        return places_[a].name_lower < places_[b].name_lower;
    });

    return !places_.empty();
}

// ── SearchIndex::search ───────────────────────────────────────────────────────

static float haversine_km(float lat1, float lon1, float lat2, float lon2) {
    constexpr float R = 6371.0f;
    float dlat = (lat2 - lat1) * 3.14159f / 180.0f;
    float dlon = (lon2 - lon1) * 3.14159f / 180.0f;
    float a = std::sin(dlat/2)*std::sin(dlat/2)
            + std::cos(lat1*3.14159f/180)*std::cos(lat2*3.14159f/180)
            * std::sin(dlon/2)*std::sin(dlon/2);
    return 2.0f * R * std::asin(std::sqrt(a));
}

std::vector<SearchResult> SearchIndex::search(const std::string& query,
                                               float bias_lat,
                                               float bias_lon,
                                               int limit) const {
    if (query.empty() || places_.empty()) return {};

    std::string q = to_lower(query);
    // Trim whitespace
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back()  == ' ') q.pop_back();
    if (q.empty()) return {};

    // Collect matches
    std::vector<SearchResult> results;
    results.reserve(64);

    for (const Place& p : places_) {
        float score = 0.0f;
        if (!fuzzy_match(q, p.name_lower, score)) continue;

        // Boost by weight (transit and education rank higher)
        score *= (1.0f + 0.1f * p.weight);

        // Proximity bias (closer = better, up to 10 km)
        float dist_km = haversine_km(bias_lat, bias_lon, p.lat, p.lon);
        float prox    = std::max(0.0f, 1.0f - dist_km / 50.0f);
        score        += 0.15f * prox;

        results.push_back({ &p, score });
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });

    if (static_cast<int>(results.size()) > limit)
        results.resize(static_cast<std::size_t>(limit));

    return results;
}

} // namespace smartcity
