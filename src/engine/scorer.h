#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "candidate.h"

namespace propico::engine {

// 辞書スコアと学習 boost を統合してスコアを確定させる
//
// final_score = base_score + boost
// boost       = log(1 + selection_count) * decay_factor
class Scorer {
public:
    explicit Scorer(float decay_factor = 0.1f) noexcept
        : decay_factor_(decay_factor) {}

    // 選択回数から boost 値を計算する
    [[nodiscard]] float compute_boost(int selection_count) const noexcept;

    // candidates の boost フィールドを boost_map に基づいて設定し、
    // final_score 降順に並べ替えて返す
    // boost_map: candidate_id → selection_count
    [[nodiscard]] std::vector<Candidate> apply(
        std::vector<Candidate>                         candidates,
        const std::unordered_map<std::string, int>&    boost_map) const;

private:
    float decay_factor_;
};

} // namespace propico::engine
