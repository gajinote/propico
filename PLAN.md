# Propico 実装計画

## 設計方針（原典 POBox より）

POBox のアーキテクチャ上の核心は以下の三点である。

1. **品詞解析なし** — 自然言語処理の知見を意図的に排除した軽量エンジン。日本語・英語・プログラミング言語を同一ロジックで処理できる
2. **2軸予測** — 辞書前方一致 × 入力履歴の2軸を統合したスコアで候補をランキング
3. **最小辞書 + 動的学習** — デフォルト辞書は絞り込み、選択履歴で候補順位を動的に更新する

---

## 現状 (2026-04-11) — **全フェーズ完了** ✅

| 成果物 | 状態 |
|---|---|
| `propico.proto` | ✅ 完了（Search / Learn / Sync RPC 定義） |
| `CMakeLists.txt` | ✅ 完了（proto 生成 + server / client / test ターゲット + SQLite3 リンク） |
| `src/server/main.cc` | ✅ 完了（Dictionary + Scorer + History に接続、全RPC実装） |
| `src/client/main.cc` | ✅ 完了（Search / Learn / Sync コマンド実装） |
| `src/engine/candidate.h` | ✅ 完了（内部 Candidate 型、`propico::engine` 名前空間） |
| `src/engine/dictionary.h/.cc` | ✅ 完了（TSV 読み込み・前方一致検索） |
| `src/engine/scorer.h/.cc` | ✅ 完了（boost 計算・final_score 降順ソート） |
| `src/engine/history.h/.cc` | ✅ 完了（SQLite ラッパー・学習とSync機能） |
| `dict/default.tsv` | ✅ 完了（日本語サンプル辞書 約190語） |
| テスト (CPPUnit) | ✅ 完了（Dictionary 9件 + History 6件 + Scorer 7件 + Sync 6件、全28件） |
| 学習機能（SQLite 永続化） | ✅ 完了 |
| 同期機能（Sync RPC） | ✅ 完了 |
| Search RPC 辞書検索・fcitx-propico 連携対応 | ✅ 完了 |

---

## フェーズ構成

### Phase 1 — インフラ整備（完了 ✅）

- [x] gRPC プロトコル定義 (`propico.proto`)
- [x] CMake ビルドシステム
- [x] サーバー / クライアント スケルトン

---

### Phase 2 — 辞書 + 予測エンジン（完了 ✅）

- [x] `src/engine/candidate.h` — 内部 Candidate 型（`propico::engine` 名前空間）
- [x] `src/engine/dictionary.h/.cc` — TSV 辞書の読み込み・前方一致検索
- [x] `src/engine/scorer.h/.cc` — `log(1+count) × decay_factor` による boost 計算
- [x] `src/server/main.cc` — Search RPC に辞書検索を接続
- [x] `dict/default.tsv` — 日本語サンプル辞書（約190語）

**動作確認済み:**
```bash
./build/propico_client search "あい"
# → 愛 [あい] score=0.95 / 挨拶 [あいさつ] score=0.90 / ...
```

---

### Phase 3 — 学習機能（動的ランキング）（完了 ✅）

**目標:** Learn RPC で選択履歴を SQLite に永続化し、サーバー再起動後も学習が引き継がれるようにする。✅ **達成**

#### 実装内容

- [x] `src/engine/history.h/.cc` — SQLite ラッパー
  - `learn_log` テーブル — 選択履歴（候補ID、プリフィックス、タイムスタンプ）
  - `score_override` テーブル — 選択カウント（候補ID → count）
  - `record(candidate_id, prefix)` — 選択を記録してカウントを +1（UPSERT）
  - `load_boost_map()` — `Scorer::apply()` 互換の `unordered_map<string, int>` を返す

- [x] `src/server/main.cc` — History 統合
  - インメモリ `unordered_map` を `History` に置き換え
  - サーバーの `db_path` を第3引数で指定可能（デフォルト: `propico_history.db`）
  - `Search` RPC 時に `history_.load_boost_map()` でブースト値を取得
  - `Learn` RPC 時に `history_.record(id, prefix)` で選択を記録

- [x] `src/client/main.cc` — Learn コマンド追加
  - `propico_client learn <candidate_id> <prefix>` で Learn RPC を呼び出し可能

#### 動作確認済み

```bash
# 1回目: 候補検索
./build/propico_client search "あい"
# → 愛 [あい] score=0.95

# 学習記録
./build/propico_client learn "あい:愛" "あい"

# 2回目: サーバー再起動後も score が上昇
./build/propico_server dict/default.tsv propico_history.db  # 再起動
./build/propico_client search "あい"
# → 愛 [あい] score=1.01931  ✅ (boost ≈ 0.0693)

# 複数回学習テスト: 3回学習で boost 累積
./build/propico_client learn "あい:愛" "あい"
./build/propico_client learn "あい:愛" "あい"
./build/propico_client search "あい"
# → 愛 [あい] score=1.08863  ✅ (boost ≈ 0.1386)
```

**計算式検証:** `boost = log(1 + selection_count) × 0.1` で正確に実装

---

### Phase 4 — テスト (CPPUnit)（完了 ✅）

- [x] `tests/test_runner.cc` — CppUnit エントリポイント
- [x] `tests/test_dictionary.cc` — 前方一致・空辞書・不正行・例外など 9件
- [x] `tests/test_scorer.cc` — boost 計算・apply・ソート順など 7件
- [x] `tests/test_learn.cc` — History テスト 6件（`:memory:` DB 使用）
- [x] `tests/test_sync.cc` — Sync テスト 6件

```bash
ctest --test-dir build
# → 100% tests passed, 28 tests passed out of 1
```

---

### Phase 5 — 同期機能（完了 ✅）

**目標:** Sync RPC で複数端末の学習データをマージする。✅ **達成**

#### 実装内容

- [x] `propico.proto` 拡張
  - `SyncEntry` メッセージ追加（candidate_id, count, updated_at）
  - `SyncResponse` を差分エントリ配列に変更

- [x] `src/engine/history.h/.cc` — Sync メソッド追加
  - `load_updated_since(since)` — since 以降の差分を返す（`WHERE updated_at > since`）
  - `merge_entries(entries)` — LWW (Last-Write-Wins) でマージ

- [x] `src/server/main.cc` — Sync RPC 実装
  - `server_timestamp` をクエリ前に取得 → クライアント返却
  - クエリ中の新規 Learn を次回で確実に捕捉

- [x] `src/client/main.cc` — sync コマンド追加
  - `propico_client sync [<last_sync_timestamp>]`
  - デフォルト (since=0) で全量取得

#### 動作確認済み（E2E）

```bash
# 初期同期（全量取得）
./build/propico_client sync 0
# → server_timestamp=<T1>  entries=0（新規サーバー）

# 学習記録 + 差分同期
./build/propico_client learn "あい:愛" "あい"
./build/propico_client sync <T1>
# → entries=0（前のタイムスタンプより新しいデータがない）

# さらに学習 + 差分確認
./build/propico_client learn "あい:愛" "あい"
./build/propico_client sync <T1>
# → entries=1  count=2  updated_at=<T2>  ✅
```

**設計特性:**
- `WHERE updated_at > since` で重複排除
- Last-Write-Wins conflict resolution でマージ
- クライアント側 DB 不要（サーバー1台多クライアント構成）

---

### Phase 6 — fcitx-propico 連携対応（完了 ✅）

**目標:** 外部プロジェクト fcitx-propico が Search RPC を呼び出して実際の変換候補を取得できる状態にする。

- [x] Search RPC ハンドラーで辞書検索を実装し、実際の変換候補を返す
  - `dict_.search(prefix, limit*2)` → `scorer_.apply(candidates, history_.load_boost_map())` → `limit` 件に絞って `SearchResponse` に設定（`src/server/main.cc`）
- [x] `build/` ディレクトリから起動したときも辞書ファイルが正しく読み込まれるよう修正
  - `cmake -B build` の configure 時に `build/dict` → `../dict` シンボリックリンクを生成（`CMakeLists.txt`）
  - 修正前は `dict/default.tsv` の相対パスが解決できず空辞書で起動していた

**動作確認済み:**

```bash
# build/ から起動しても辞書が読み込まれる
cd build && ./propico_server
./propico_client search "あい"
# → 愛 [あい] score=0.95 / 挨拶 [あいさつ] score=0.90 / ...
```

---

## 優先順位と依存関係

```
Phase 1 ✅ → Phase 2 ✅ → Phase 3 ✅ → Phase 4 ✅ → Phase 5 ✅ → Phase 6 ✅
```

**全フェーズ完了**

---

## コミット履歴

- `2fe991b` - implement Phase 5: Sync RPC for multi-device learning data synchronization
- `a1e0f60` - update PLAN.md: document Phase 3 completion and verify implementation details
- `198b748` - enhance client: add Learn RPC support for testing
- `4442b77` - implement Phase 3: SQLite-based learning persistence
- `af873ec` - add cppunit and unit tests
- `1bc25d3` - add dictionary
- `ebffbac` - add build engine and dictionary
- `c231c5c` - Update docs based on original POBox architecture
