# pobox-neo 実装計画

## 設計方針（原典 POBox より）

POBox のアーキテクチャ上の核心は以下の三点である。

1. **品詞解析なし** — 自然言語処理の知見を意図的に排除した軽量エンジン。日本語・英語・プログラミング言語を同一ロジックで処理できる
2. **2軸予測** — 辞書前方一致 × 入力履歴の2軸を統合したスコアで候補をランキング
3. **最小辞書 + 動的学習** — デフォルト辞書は絞り込み、選択履歴で候補順位を動的に更新する

---

## 現状 (2026-04-03)

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

### Phase 2 — 辞書 + 予測エンジン

**目標:** Search RPC が実際に候補を返せるようにする。

POBox 原典の「辞書前方一致予測」を実装する。品詞解析は行わない。

#### 2-1. 辞書データ構造

```
src/engine/
├── dictionary.h / .cc   ← TSV 辞書の読み込み・前方一致検索
├── candidate.h          ← Candidate 内部表現（id / text / reading / base_score）
└── scorer.h / .cc       ← 辞書スコア + 学習 boost の統合
```

- 辞書形式: `読み\t表記\t基本スコア`（TSV）
- 検索: UTF-8 文字列前方一致（将来的に曖昧一致へ拡張）
- 基本スコアは出現頻度ベース（0.0〜1.0）

#### 2-2. Search RPC への組み込み

- `PoBoxServiceImpl::Search` に辞書検索を接続
- 結果をスコア降順にソートして `SearchResponse` に詰める
- `max_candidates` で件数上限を適用する

#### 完了条件

```bash
./build/pobox_client "あい"
# → 愛  [あい]  score=0.95
#    間  [あい]  score=0.80
```

---

### Phase 3 — 学習機能（動的ランキング）

**目標:** Learn RPC で選択履歴を永続化し、使うほど賢くなる動的ランキングを実現する。

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

#### 3-2. スコア計算式

```
final_score = base_score + boost
boost       = log(1 + selection_count) × decay_factor
```

`decay_factor` は設定可能（デフォルト 0.1）。

#### 3-3. Learn RPC への組み込み

- `LearnRequest` を受けて `learn_log` に記録
- `score_override.boost` を更新
- 次回 `Search` 時に `scorer` が boost を基本スコアに加算

#### 追加依存パッケージ

```bash
sudo apt install -y libsqlite3-dev
```

---

### Phase 4 — テスト (CPPUnit)

**目標:** エンジンの主要ロジックを自動テストで保護する。

```
tests/
├── test_runner.cc        ← main + CppUnit::TextUi::TestRunner
├── test_dictionary.cc    ← TSV 辞書読み込み・前方一致検索
├── test_scorer.cc        ← スコア計算式の単体テスト
└── test_learn.cc         ← 学習ログ記録・boost 更新
```

```bash
cmake --build build && ctest --test-dir build
```

CPPUnit が未インストールの環境ではテストターゲットをスキップ（CMake 設定で対応済み）。

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
Phase 1 ✅ → Phase 2 → Phase 4（並行可）→ Phase 3 → Phase 5
```

- Phase 2 と Phase 4 の骨格は並行して進められる
- Phase 3（SQLite）は Phase 2 の辞書検索完了後に着手する
- Phase 5 は Phase 3 が安定してから着手する
