// maneuver.cpp — Simplified turn-by-turn step builder (Phase 2/3)
// Computes heading change between consecutive edges to classify turns.
#include "maneuver.h"
#include <cmath>
#include <algorithm>

namespace smartcity {

static constexpr float PI = 3.14159265f;
static constexpr float DEG2RAD = PI / 180.0f;

// Bearing from (lat1,lon1) to (lat2,lon2) in degrees [0,360)
static float bearing(float lat1, float lon1, float lat2, float lon2) {
    float dlon = (lon2 - lon1) * DEG2RAD;
    float rlat1 = lat1 * DEG2RAD, rlat2 = lat2 * DEG2RAD;
    float y = std::sin(dlon) * std::cos(rlat2);
    float x = std::cos(rlat1) * std::sin(rlat2)
            - std::sin(rlat1) * std::cos(rlat2) * std::cos(dlon);
    float b = std::atan2(y, x) * (180.0f / PI);
    return std::fmod(b + 360.0f, 360.0f);
}

// Classify turn from heading `from` to heading `to`
static std::string classify_turn(float from_hdg, float to_hdg) {
    float diff = std::fmod(to_hdg - from_hdg + 540.0f, 360.0f) - 180.0f;
    if (diff >  110.0f) return "turn-right-sharp";
    if (diff >   35.0f) return "turn-right";
    if (diff >   10.0f) return "keep-right";
    if (diff <  -110.0f) return "turn-left-sharp";
    if (diff <   -35.0f) return "turn-left";
    if (diff <   -10.0f) return "keep-left";
    return "straight";
}

std::vector<RouteStep> build_steps(const std::vector<uint32_t>& edge_path,
                                    const Graph& g) {
    std::vector<RouteStep> steps;
    if (edge_path.empty()) return steps;

    // ── Group consecutive edges on the same road into one step ────────────────
    struct Seg {
        std::string road_name;
        float distance_m = 0;
        float duration_s = 0;
        float lat, lon;      // start point
        float end_lat, end_lon;
        float in_bearing  = 0;
        float out_bearing = 0;
    };

    std::vector<Seg> segs;
    Seg cur_seg;

    for (std::size_t i = 0; i < edge_path.size(); ++i) {
        const Edge& e  = g.edges[edge_path[i]];
        const Node& na = g.nodes[e.from];
        const Node& nb = g.nodes[e.to];
        std::string name = g.edge_name(e);
        if (name.empty()) name = g.edge_ref(e);

        float hdg = bearing(na.lat, na.lon, nb.lat, nb.lon);

        if (i == 0) {
            cur_seg.road_name  = name;
            cur_seg.lat        = na.lat;
            cur_seg.lon        = na.lon;
            cur_seg.in_bearing = hdg;
        }

        // Road change → flush segment
        if (i > 0 && name != cur_seg.road_name) {
            cur_seg.out_bearing = hdg;
            segs.push_back(cur_seg);
            cur_seg = {};
            cur_seg.road_name  = name;
            cur_seg.lat        = na.lat;
            cur_seg.lon        = na.lon;
            cur_seg.in_bearing = hdg;
        }

        cur_seg.distance_m  += e.length_m;
        cur_seg.duration_s  += e.free_flow_s();
        cur_seg.end_lat      = nb.lat;
        cur_seg.end_lon      = nb.lon;
        cur_seg.out_bearing  = hdg;
    }
    segs.push_back(cur_seg);

    // ── Convert segments to RouteSteps ────────────────────────────────────────
    for (std::size_t i = 0; i < segs.size(); ++i) {
        RouteStep step;
        step.road_name  = segs[i].road_name;
        step.distance_m = segs[i].distance_m;
        step.duration_s = segs[i].duration_s;
        step.lat        = segs[i].lat;
        step.lon        = segs[i].lon;

        if (i == 0) {
            step.maneuver = "depart";
        } else {
            float prev_hdg = segs[i - 1].out_bearing;
            float this_hdg = segs[i].in_bearing;
            step.maneuver = classify_turn(prev_hdg, this_hdg);
        }
        steps.push_back(std::move(step));
    }

    // Arrival step
    if (!segs.empty()) {
        RouteStep arrive;
        arrive.maneuver   = "arrive";
        arrive.road_name  = segs.back().road_name;
        arrive.distance_m = 0;
        arrive.duration_s = 0;
        arrive.lat        = segs.back().end_lat;
        arrive.lon        = segs.back().end_lon;
        steps.push_back(arrive);
    }

    return steps;
}

} // namespace smartcity
