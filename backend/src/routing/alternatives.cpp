// alternatives.cpp — Multi-alternative route computation (Phase 5)
#include "alternatives.h"
#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace smartcity {

AlternativeRoutes compute_alternatives(uint32_t from, uint32_t to,
                                        const Graph& g,
                                        const WeightStore& ws,
                                        int max_alts,
                                        float penalty_factor,
                                        float max_stretch,
                                        float max_overlap) {
    AlternativeRoutes alts;
    DijkstraRouter router(g, ws);
    auto best = router.route(from, to, true, false);
    if (!best.found) return alts;

    alts.routes.push_back(best);
    if (max_alts <= 1) return alts;

    auto current_snap = ws.current();
    if (!current_snap) return alts;

    auto penalized_snap = std::make_shared<WeightSnapshot>(*current_snap);

    std::vector<std::unordered_set<uint32_t>> accepted_edge_sets;
    accepted_edge_sets.push_back(std::unordered_set<uint32_t>(best.edge_path.begin(), best.edge_path.end()));

    auto calc_overlap = [](const std::unordered_set<uint32_t>& a, const std::unordered_set<uint32_t>& b) -> float {
        if (a.empty() || b.empty()) return 1.0f;
        int shared = 0;
        for (uint32_t ei : a) {
            if (b.count(ei)) ++shared;
        }
        return static_cast<float>(shared) / static_cast<float>(std::min(a.size(), b.size()));
    };

    // Progressive penalty rounds: protect terminals so departure/arrival roads never block search
    const float factors[]   = { 1.35f, 1.60f, 1.95f, 2.40f, 3.00f, 4.00f };
    const float overlaps[]  = { 0.82f, 0.88f, 0.92f, 0.95f, 0.97f, 0.99f };
    const float stretches[] = { 1.70f, 2.10f, 2.50f, 3.00f, 3.50f, 4.20f };

    for (int iter = 0; iter < 6 && static_cast<int>(alts.routes.size()) < max_alts; ++iter) {
        float factor = factors[iter];
        float overlap_limit = overlaps[iter];
        float stretch_limit = stretches[iter];

        // Penalize intermediate edges of all already accepted routes
        for (const auto& r : alts.routes) {
            size_t n = r.edge_path.size();
            size_t p_start = std::min<size_t>(2, n / 4);
            size_t p_end   = n > p_start ? (n - std::min<size_t>(2, n / 4)) : n;

            for (size_t idx = p_start; idx < p_end; ++idx) {
                uint32_t ei = r.edge_path[idx];
                if (ei < penalized_snap->weights.size()) {
                    penalized_snap->weights[ei] *= factor;
                }
            }
        }

        WeightStore alt_ws(g.edge_count());
        alt_ws.publish(penalized_snap);
        DijkstraRouter alt_router(g, alt_ws);

        auto cand = alt_router.route(from, to, true, false);
        if (!cand.found) continue;

        // Recalculate duration under UNPENALIZED snapshot
        float real_dur = 0.0f;
        float real_ff  = 0.0f;
        for (uint32_t ei : cand.edge_path) {
            real_dur += (ei < current_snap->weights.size()) ? current_snap->weights[ei] : g.edges[ei].free_flow_s();
            real_ff  += g.edges[ei].free_flow_s();
        }
        cand.duration_s  = real_dur;
        cand.free_flow_s = real_ff;

        if (cand.duration_s > best.duration_s * stretch_limit) {
            continue;
        }

        std::unordered_set<uint32_t> cand_set(cand.edge_path.begin(), cand.edge_path.end());
        bool distinct = true;
        for (const auto& accepted : accepted_edge_sets) {
            if (calc_overlap(cand_set, accepted) >= overlap_limit) {
                distinct = false;
                break;
            }
        }

        if (distinct && !cand.edge_path.empty()) {
            accepted_edge_sets.push_back(cand_set);
            alts.routes.push_back(std::move(cand));
        }
    }

    // Fallback: If still under max_alts, route via an intermediate detour node off the main corridor
    if (static_cast<int>(alts.routes.size()) < max_alts && best.node_path.size() >= 3) {
        DijkstraRouter direct_router(g, ws);
        size_t n_nodes = best.node_path.size();
        std::unordered_set<uint32_t> best_node_set(best.node_path.begin(), best.node_path.end());

        const size_t fractions[][2] = { {1, 3}, {2, 3}, {1, 2}, {1, 4}, {3, 4} };

        for (auto frac : fractions) {
            if (static_cast<int>(alts.routes.size()) >= max_alts) break;

            size_t mid_idx = (n_nodes * frac[0]) / frac[1];
            if (mid_idx >= n_nodes) continue;
            uint32_t mid_node = best.node_path[mid_idx];

            uint32_t detour_node = UINT32_MAX;
            for (uint32_t out1 : g.out_edges(mid_node)) {
                uint32_t n1 = g.edges[out1].to;
                if (!best_node_set.count(n1)) {
                    detour_node = n1;
                    break;
                }
                for (uint32_t out2 : g.out_edges(n1)) {
                    uint32_t n2 = g.edges[out2].to;
                    if (!best_node_set.count(n2)) {
                        detour_node = n2;
                        break;
                    }
                }
                if (detour_node != UINT32_MAX) break;
            }

            if (detour_node != UINT32_MAX && detour_node < g.node_count()) {
                auto leg1 = direct_router.route(from, detour_node, true, false);
                auto leg2 = direct_router.route(detour_node, to, true, false);

                if (leg1.found && leg2.found) {
                    DijkstraResult combined;
                    combined.found = true;
                    combined.node_path = leg1.node_path;
                    if (leg2.node_path.size() > 1) {
                        combined.node_path.insert(combined.node_path.end(),
                                                  leg2.node_path.begin() + 1, leg2.node_path.end());
                    }
                    combined.edge_path = leg1.edge_path;
                    combined.edge_path.insert(combined.edge_path.end(),
                                              leg2.edge_path.begin(), leg2.edge_path.end());
                    combined.distance_m  = leg1.distance_m + leg2.distance_m;
                    combined.duration_s  = leg1.duration_s + leg2.duration_s;
                    combined.free_flow_s = leg1.free_flow_s + leg2.free_flow_s;
                    combined.stats.nodes_settled = leg1.stats.nodes_settled + leg2.stats.nodes_settled;
                    combined.stats.edges_relaxed = leg1.stats.edges_relaxed + leg2.stats.edges_relaxed;

                    std::unordered_set<uint32_t> cand_set(combined.edge_path.begin(), combined.edge_path.end());
                    bool distinct = true;
                    for (const auto& accepted : accepted_edge_sets) {
                        if (calc_overlap(cand_set, accepted) >= 0.99f) {
                            distinct = false;
                            break;
                        }
                    }
                    if (distinct) {
                        accepted_edge_sets.push_back(cand_set);
                        alts.routes.push_back(std::move(combined));
                    }
                }
            }
        }
    }

    return alts;
}

} // namespace smartcity
