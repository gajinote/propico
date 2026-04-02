# pobox-neo 実装計画

## 現状 (2026-04-02)

| 成果物 | 状態 |
|---|---|
| `pobox.proto` | ✅ 完了（Search / Learn / Sync RPC 定義） |
| `CMakeLists.txt` | ✅ 完了（proto 生成 + server/client/test ターゲット） |
| `src/server/main.cc` | ✅ スケルトン完了（ログ出力のみ） |
| `src/client/main.cc` | ✅ スケルトン完了（Search RPC 呼び出し） |
| ビルド | ✅ 成功（`build/pobox_server`, `build/pobox_client` 生成済み） |
| 予測エンジン | ❌ 未実装 |
| 学習機能 | ❌ 未実装 |
| テスト (CPPUnit) | ❌ 未実装 |

---

## フェーズ構成

### Phase 1 — インフラ整備（完了）

- [x] gRPC プロトコル定義 (`pobox.proto`)
- [x] CMake ビルドシステム
- [x] サーバー / クライアント スケルトン

---

### Phase 2 — 辞書・検索エンジン

**目標:** Search RPC が実際に候補を返せるようにする。

#### 2-1. 辞書データ構造

```
src/engine/
├── dictionary.h / .cc   ← 辞書の読み込み・検索
├── candidate.h          ← Candidate の内部表現
└── trie.h / .cc         ← プレフィックス検索用 Trie 木
```

- 辞書形式: TSV（`読み\t表記\t初期スコア`）
- 検索アルゴリズム: Trie 木によるプレフィックス一致
- スコア: 出現頻度ベース（初期値は辞書側で付与）

#### 2-2. Search RPC への組み込み

- `PoBoxServiceImpl::Search` に辞書検索を接続
- 結果をスコア降順にソートして `SearchResponse` に詰める
- `max_candidates` で件数上限を反映する

#### 完了条件

```bash
./build/pobox_client "あい"
# → 愛  [あい]  score=0.95
#    間  [あい]  score=0.80
#    ...
```

---

### Phase 3 — 学習機能

**目標:** Learn RPC で選択履歴を永続化し、スコアに反映する。

#### 3-1. ストレージ

- SQLite（`libsqlite3-dev`）をローカル永続ストアとして採用
- テーブル設計:

```sql
CREATE TABLE learn_log (
    candidate_id TEXT NOT NULL,
    prefix       TEXT NOT NULL,
    selected_at  INTEGER NOT NULL  -- Unix 秒
);

CREATE TABLE score_override (
    candidate_id TEXT PRIMARY KEY,
    boost        REAL NOT NULL DEFAULT 0.0
);
```

#### 3-2. スコア計算

```
最終スコア = 辞書スコア + boost
boost      = log(1 + 選択回数) × 減衰係数
```

#### 3-3. Learn RPC への組み込み

- `LearnRequest` を受けて `learn_log` に記録
- `score_override` を更新
- 次回 Search 時に boost を加算

---

### Phase 4 — テスト (CPPUnit)

**目標:** エンジンの主要ロジックを自動テストで保護する。

```
tests/
├── test_runner.cc        ← main + CppUnit::TextUi::TestRunner
├── test_trie.cc          ← Trie 検索の単体テスト
├── test_dictionary.cc    ← 辞書読み込みの単体テスト
└── test_score.cc         ← スコア計算のテスト
```

- `cmake --build build && ctest --test-dir build` で全テスト実行
- CPPUnit が未インストールの環境ではテストターゲットをスキップ（既存の CMake 設定で対応済み）

---

### Phase 5 — 同期機能（将来）

**目標:** Sync RPC で複数端末の学習データをマージする。

- サーバー側に `last_sync_timestamp` 以降の差分を返すロジックを実装
- クライアント側に差分を受け取り `score_override` にマージする処理を実装
- 競合解決ポリシー: タイムスタンプが新しい方を優先（Last-Write-Wins）

---

## 優先順位

```
Phase 2（検索エンジン）→ Phase 4（テスト）→ Phase 3（学習）→ Phase 5（同期）
```

Phase 2 と Phase 4 は並行して進められる。
Phase 3 は Phase 2 の完了後に着手する。

---

## 追加依存パッケージ（Phase 3 以降）

```bash
sudo apt install -y libsqlite3-dev
```

| パッケージ | 用途 |
|---|---|
| `libsqlite3-dev` | 学習データの永続ストレージ |
