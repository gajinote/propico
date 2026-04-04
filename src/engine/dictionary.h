#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "candidate.h"

namespace pobox::engine {

// TSV 辞書ファイル（読み\t表記\t基本スコア）を読み込み、
// プレフィックス前方一致で候補を返す辞書クラス
class Dictionary {
public:
    // TSV ファイルを読み込む。ファイルが存在しない場合は std::runtime_error を投げる
    void load(const std::filesystem::path& tsv_path);

    // prefix で始まる読みを持つ候補を返す（スコア降順、最大 max_results 件）
    // max_results == 0 のときは件数制限なし
    [[nodiscard]] std::vector<Candidate> search(const std::string& prefix,
                                                std::size_t        max_results = 0) const;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    // reading でソート済みの候補列
    std::vector<Candidate> entries_;
};

} // namespace pobox::engine
