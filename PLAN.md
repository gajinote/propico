# pobox-neo 実装計画

## 設計方針（原典 POBox より）

POBox のアーキテクチャ上の核心は以下の三点である。

1. **品詞解析なし** — 自然言語処理の知見を意図的に排除した軽量エンジン。日本語・英語・プログラミング言語を同一ロジックで処理できる
2. **2軸予測** — 辞書前方一致 × 入力履歴の2軸を統合したスコアで候補をランキング
3. **最小辞書 + 動的学習** — デフォルト辞書は絞り込み、選択履歴で候補順位を動的に更新する

---

## 現状 (2026-04-06)

| 成果物 | 状態 |
|---|---|
| `pobox.proto` | ✅ 完了（Search / Learn / Sync RPC 定義） |
| `CMakeLists.txt` | ✅ 完了（proto 生成 + server / client / test ターゲット） |
| `src/server/main.cc` | ✅ 完了（Dictionary + Scorer に接続、Learn はインメモリ暫定） |
| `src/client/main.cc` | ✅ 完了（Search RPC 呼び出し・候補表示） |
| `src/engine/candidate.h` | ✅ 完了（内部 Candidate 型、`pobox::engine` 名前空間） |
| `src/engine/dictionary.h/.cc` | ✅ 完了（TSV 読み込み・前方一致検索） |
| `src/engine/scorer.h/.cc` | ✅ 完了（boost 計算・final_score 降順ソート） |
| `dict/default.tsv` | ✅ 完了（日本語サンプル辞書 約190語） |
| テスト (CPPUnit) | ✅ 完了（Dictionary 9件 + Scorer 7件、全16件 OK） |
| 学習機能（SQLite 永続化） | ❌ 未実装（現状はインメモリのみ） |
| 同期機能 | ❌ 未実装（将来） |

---

## フェーズ構成

### Phase 1 — インフラ整備（完了）

- [x] gRPC プロトコル定義 (`pobox.proto`)
- [x] CMake ビルドシステム
- [x] サーバー / クライアント スケルトン

---

### Phase 2 — 辞書 + 予測エンジン（完了）

- [x] `src/engine/candidate.h` — 内部 Candidate 型（`pobox::engine` 名前空間）
- [x] `src/engine/dictionary.h/.cc` — TSV 辞書の読み込み・前方一致検索
- [x] `src/engine/scorer.h/.cc` — `log(1+count) × decay_factor` による boost 計算
- [x] `src/server/main.cc` — Search RPC に辞書検索を接続
- [x] `dict/default.tsv` — 日本語サンプル辞書（約190語）

**動作確認済み:**
```bash
./build/pobox_client "あい"
# → 愛 [あい] score=0.95 / 挨拶 [あいさつ] score=0.90 / ...
```

**備考:** Learn RPC は現在インメモリ（`unordered_map`）で boost を保持している。
サーバー再起動で学習データは消える。Phase 3 で SQLite に置き換える。

---

### Phase 4 — テスト (CPPUnit)（完了）

- [x] `tests/test_runner.cc` — CppUnit エントリポイント
- [x] `tests/test_dictionary.cc` — 前方一致・空辞書・不正行・例外など 9件
- [x] `tests/test_scorer.cc` — boost 計算・apply・ソート順など 7件

```bash
ctest --test-dir build
# → 100% tests passed, 0 tests failed out of 1
```

Phase 3 実装後に `tests/test_learn.cc` を追加する。

---

### Phase 3 — 学習機能（動的ランキング）← **次のステップ**

**目標:** Learn RPC で選択履歴を SQLite に永続化し、サーバー再起動後も学習が引き継がれるようにする。

POBox 原典の「使用頻度による動的ランキング」と「複合語の文脈学習」に対応する。

#### 3-1. ストレージ設計

SQLite（`libsqlite3-dev`）をローカル永続ストアとして採用。

```sql
-- 選択履歴（生ログ）
CREATE TABLE learn_log (
    candidate_id TEXT NOT NULL,
    prefix       TEXT NOT NULL,
    selected_at  INTEGER NOT NULL  -- Unix 秒
);

-- スコアブースト（集計済みキャッシュ）
CREATE TABLE score_override (
    candidate_id TEXT PRIMARY KEY,
    boost        REAL NOT NULL DEFAULT 0.0,
    updated_at   INTEGER NOT NULL
);

-- 複合語学習（"つなげて学習"相当）
CREATE TABLE compound_log (
    first_id   TEXT NOT NULL,
    second_id  TEXT NOT NULL,
    count      INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (first_id, second_id)
);
```

#### 3-2. 実装ファイル

```
src/engine/
└── history.h / .cc   ← SQLite ラッパー（learn_log / score_override の読み書き）
```

#### 3-3. スコア計算式

```
final_score = base_score + boost
boost       = log(1 + selection_count) × decay_factor
```

`decay_factor` は設定可能（デフォルト 0.1）。

#### 3-4. Learn RPC への組み込み

- `LearnRequest` を受けて `learn_log` に記録
- `score_override` の boost を更新
- Search 時に `history` から boost_map を取得し `scorer_.apply()` に渡す
- サーバーの `unordered_map<boost_map>` を SQLite ベースに置き換える

#### 追加依存パッケージ

```bash
sudo apt install -y libsqlite3-dev
```

#### 完了条件

```bash
# 1回目: サーバー起動・候補選択
./build/pobox_client "あい"   # → 愛 score=0.95
# Learn で "愛" を学習

# 2回目: サーバー再起動後も "愛" のスコアが上昇している
./build/pobox_server dict/default.tsv  # 再起動
./build/pobox_client "あい"   # → 愛 score > 0.95
```

---

### Phase 5 — 同期機能（将来）

**目標:** Sync RPC で複数端末の学習データをマージする。

POBox 原典の「別端末で学習した内容を同期」に対応する。

- サーバー側: `last_sync_timestamp` 以降の `score_override` 差分を返す
- クライアント側: 差分を受け取りローカル SQLite にマージ
- 競合解決: Last-Write-Wins（タイムスタンプが新しい方を優先）

---

## 優先順位と依存関係

```
Phase 1 ✅ → Phase 2 ✅ → Phase 4 ✅ → Phase 3（次） → Phase 5
```

- Phase 3 完了後、`tests/test_learn.cc` を追加して Phase 4 を拡充する
- Phase 5 は Phase 3 が安定してから着手する
