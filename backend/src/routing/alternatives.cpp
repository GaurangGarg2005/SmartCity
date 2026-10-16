// alternatives.cpp — Phase 5 stub. Penalty-method implementation coming in Phase 5.
#include "alternatives.h"

namespace smartcity {

AlternativeRoutes compute_alternatives(uint32_t from, uint32_t to,
                                        const Graph& g,
                                        const WeightStore& ws,
                                        int /*max_alts*/,
                                        float /*penalty_factor*/,
                                        float /*max_stretch*/,
                                        float /*max_overlap*/) {
    // Phase 5 stub: just return the best route for now
    DijkstraRouter router(g, ws);
    auto best = router.route(from, to, true, false);
    AlternativeRoutes alts;
    if (best.found) alts.routes.push_back(std::move(best));
    return alts;
}

} // namespace smartcity
