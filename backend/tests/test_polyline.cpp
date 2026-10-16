// test_polyline.cpp — Polyline encode/decode tests (Phase 2)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../src/routing/polyline.h"

using namespace smartcity;
using Catch::Approx;

TEST_CASE("Polyline encode/decode roundtrip", "[polyline]") {
    std::vector<std::pair<float,float>> pts = {
        {28.5705f, 77.3219f},   // JIIT Noida
        {28.5634f, 77.3382f},   // Botanical Garden Metro
        {28.5706f, 77.3214f},   // Sector 18 Metro
    };

    std::string encoded = encode_polyline6(pts);
    REQUIRE_FALSE(encoded.empty());

    auto decoded = decode_polyline6(encoded);
    REQUIRE(decoded.size() == pts.size());

    for (std::size_t i = 0; i < pts.size(); ++i) {
        REQUIRE(decoded[i].first  == Approx(pts[i].first ).epsilon(1e-5));
        REQUIRE(decoded[i].second == Approx(pts[i].second).epsilon(1e-5));
    }
}

TEST_CASE("Polyline: empty input", "[polyline]") {
    std::string enc = encode_polyline6({});
    REQUIRE(enc.empty());
    auto dec = decode_polyline6("");
    REQUIRE(dec.empty());
}
