#pragma once
// polyline.h — Google-style Polyline6 encode/decode
// Precision: 1e-6 degrees (~0.11 m), sufficient for routing.
#include <string>
#include <vector>
#include <utility>

namespace smartcity {

/// Encode a list of (lat, lon) pairs to Polyline6 format.
std::string encode_polyline6(const std::vector<std::pair<float,float>>& pts);

/// Decode a Polyline6 string to (lat, lon) pairs.
std::vector<std::pair<float,float>> decode_polyline6(const std::string& enc);

} // namespace smartcity
