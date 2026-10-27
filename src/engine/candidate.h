#pragma once

#include <string>

namespace propico::engine {

// エンジン内部で扱う候補の表現
// proto の Candidate とは独立しており、scorer がスコアを確定してから変換する
struct Candidate {
    std::string id;         // 安定識別子: "reading:text"
    std::string text;       // 表示テキスト（漢字・かな交じり文など）
    std::string reading;    // 読み（ひらがな）
    float       base_score; // 辞書由来の基本スコア（0.0〜1.0）
    float       boost;      // 学習による加算分（Phase 3 で設定）

    // 最終スコア = base_score + boost
    [[nodiscard]] float final_score() const noexcept { return base_score + boost; }
};

// 候補の表示順序: final_score 降順 → 読みが短い（入力に近い）順
// 同順位は std::stable_sort により辞書の並びが保たれる
inline bool ranks_before(const Candidate& a, const Candidate& b) noexcept {
    if (a.final_score() != b.final_score()) return a.final_score() > b.final_score();
    return a.reading.size() < b.reading.size();
}

} // namespace propico::engine
