#pragma once
// search_index.h — Typo-tolerant prefix search over places.json
// Uses a sorted array + binary-search for prefix matching
// and simple edit-distance for typo tolerance (up to 1 deletion/substitution).

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace smartcity {

struct Place {
    std::string id;
    std::string name;
    std::string name_lower;   // for fast comparisons
    std::string type;         // education / transit / road / …
    std::string address;
    float       lat, lon;
    float       weight;       // higher = more important hotspot
};

struct SearchResult {
    const Place* place;
    float        score;      // higher = better match
};

class SearchIndex {
public:
    SearchIndex() = default;

    /// Load from data/places.json.  Returns false on error.
    bool load(const std::string& json_path);

    /// Search for up to `limit` results matching `query`.
    /// `bias_lat/lon` biases results towards the map centre (distance tie-break).
    std::vector<SearchResult> search(const std::string& query,
                                     float bias_lat, float bias_lon,
                                     int limit = 8) const;

    std::size_t size() const { return places_.size(); }

    /// All places (for vehicle OD sampling in simulator)
    const std::vector<Place>& places() const { return places_; }

private:
    std::vector<Place>  places_;     // sorted by name_lower for binary search
    std::vector<size_t> sorted_idx_; // indices into places_ sorted by name_lower

    static bool fuzzy_match(const std::string& query_lower,
                             const std::string& name_lower,
                             float& score);
};

} // namespace smartcity
