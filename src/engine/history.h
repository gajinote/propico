#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace propico::engine {

// 同期エントリ（SyncResponse のペイロード）
struct SyncRow {
    std::string  candidate_id;
    int          count;
    std::int64_t updated_at;
};

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

    // since 以降に更新された score_override エントリを返す
    // since == 0 → 全件取得 / since > 0 → WHERE updated_at > since
    [[nodiscard]] std::vector<SyncRow> load_updated_since(std::int64_t since) const;

    // 外部から受け取った差分をマージする（Last-Write-Wins）
    // remote.updated_at > local.updated_at の場合のみ上書き
    void merge_entries(const std::vector<SyncRow>& entries);

    // コピー禁止・ムーブのみ可
    History(const History&)            = delete;
    History& operator=(const History&) = delete;

private:
    sqlite3*           db_ = nullptr;
    mutable std::mutex mu_;

    void init_schema();
};

} // namespace propico::engine
