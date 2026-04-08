#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct sqlite3;

namespace pobox::engine {

// 選択履歴を SQLite に永続化するクラス
//
// テーブル構成:
//   learn_log     — 生ログ（candidate_id, prefix, selected_at）
//   score_override — 選択回数キャッシュ（candidate_id → count）
//
// Scorer::apply() と互換性を保つため load_boost_map() は
// unordered_map<candidate_id, selection_count> を返す
class History {
public:
    // db_path: SQLite ファイルパス。":memory:" でインメモリ動作
    explicit History(const std::string& db_path);
    ~History();

    // 選択を記録する（learn_log に追記・score_override のカウントを +1）
    void record(const std::string& candidate_id, const std::string& prefix);

    // candidate_id → selection_count のマップを返す（Scorer::apply に渡す用）
    [[nodiscard]] std::unordered_map<std::string, int> load_boost_map() const;

    // コピー禁止・ムーブのみ可
    History(const History&)            = delete;
    History& operator=(const History&) = delete;

private:
    sqlite3* db_ = nullptr;

    void init_schema();
};

} // namespace pobox::engine
