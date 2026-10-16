// polyline.cpp — Google Polyline6 encode/decode
// Reference: https://developers.google.com/maps/documentation/utilities/polylinealgorithm
#include "polyline.h"
#include <cmath>
#include <cstdint>

namespace smartcity {

static constexpr double SCALE = 1e6;

static void encode_value(int32_t value, std::string& out) {
    int32_t v = value < 0 ? ~(value << 1) : (value << 1);
    while (v >= 0x20) {
        out += static_cast<char>((0x20 | (v & 0x1F)) + 63);
        v >>= 5;
    }
    out += static_cast<char>(v + 63);
}

std::string encode_polyline6(const std::vector<std::pair<float,float>>& pts) {
    std::string out;
    out.reserve(pts.size() * 10);
    int32_t prev_lat = 0, prev_lon = 0;
    for (auto [lat, lon] : pts) {
        int32_t ilat = static_cast<int32_t>(std::round(static_cast<double>(lat) * SCALE));
        int32_t ilon = static_cast<int32_t>(std::round(static_cast<double>(lon) * SCALE));
        encode_value(ilat - prev_lat, out);
        encode_value(ilon - prev_lon, out);
        prev_lat = ilat;
        prev_lon = ilon;
    }
    return out;
}

std::vector<std::pair<float,float>> decode_polyline6(const std::string& enc) {
    std::vector<std::pair<float,float>> pts;
    int32_t lat = 0, lon = 0;
    std::size_t i = 0;

    auto decode_next = [&](int32_t& coord) -> bool {
        int32_t result = 0, shift = 0;
        while (i < enc.size()) {
            int32_t b = static_cast<uint8_t>(enc[i++]) - 63;
            result |= (b & 0x1F) << shift;
            shift  += 5;
            if (b < 0x20) {
                coord += (result & 1) ? ~(result >> 1) : (result >> 1);
                return true;
            }
        }
        return false;
    };

    while (i < enc.size()) {
        if (!decode_next(lat)) break;
        if (!decode_next(lon)) break;
        pts.emplace_back(static_cast<float>(lat / SCALE),
                         static_cast<float>(lon / SCALE));
    }
    return pts;
}

} // namespace smartcity
