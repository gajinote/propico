#include "scorer.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace pobox::engine {

float Scorer::compute_boost(int selection_count) const noexcept {
    if (selection_count <= 0) return 0.0f;
    return std::log1p(static_cast<float>(selection_count)) * decay_factor_;
}

std::vector<Candidate> Scorer::apply(
    std::vector<Candidate>                      candidates,
    const std::unordered_map<std::string, int>& boost_map) const
{
    for (auto& c : candidates) {
        if (auto it = boost_map.find(c.id); it != boost_map.end()) {
            c.boost = compute_boost(it->second);
        }
    }

    std::ranges::sort(candidates, std::greater<>{},
                      [](const Candidate& c) { return c.final_score(); });

    return candidates;
}

} // namespace pobox::engine
