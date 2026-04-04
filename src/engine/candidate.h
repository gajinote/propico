#pragma once

#include <string>

namespace pobox::engine {

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

} // namespace pobox::engine
