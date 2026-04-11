# pobox-neo 実装計画

## 設計方針（原典 POBox より）

POBox のアーキテクチャ上の核心は以下の三点である。

1. **品詞解析なし** — 自然言語処理の知見を意図的に排除した軽量エンジン。日本語・英語・プログラミング言語を同一ロジックで処理できる
2. **2軸予測** — 辞書前方一致 × 入力履歴の2軸を統合したスコアで候補をランキング
3. **最小辞書 + 動的学習** — デフォルト辞書は絞り込み、選択履歴で候補順位を動的に更新する

---

## 現状 (2026-04-11)

| 成果物 | 状態 |
|---|---|
| `pobox.proto` | ✅ 完了（Search / Learn / Sync RPC 定義） |
| `CMakeLists.txt` | ✅ 完了（proto 生成 + server / client / test ターゲット + SQLite3 リンク） |
| `src/server/main.cc` | ✅ 完了（Dictionary + Scorer + History に接続） |
| `src/client/main.cc` | ✅ 完了（Search + Learn RPC 呼び出し・候補表示・学習記録） |
| `src/engine/candidate.h` | ✅ 完了（内部 Candidate 型、`pobox::engine` 名前空間） |
| `src/engine/dictionary.h/.cc` | ✅ 完了（TSV 読み込み・前方一致検索） |
| `src/engine/scorer.h/.cc` | ✅ 完了（boost 計算・final_score 降順ソート） |
| `src/engine/history.h/.cc` | ✅ 完了（SQLite ラッパー・learn_log / score_override） |
| `dict/default.tsv` | ✅ 完了（日本語サンプル辞書 約190語） |
| テスト (CPPUnit) | ✅ 完了（Dictionary 9件 + History 6件 + Scorer 7件、全22件 OK） |
| 学習機能（SQLite 永続化） | ✅ 完了（サーバー再起動後もスコア引き継ぎ確認） |
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
./build/pobox_client search "あい"
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
  - サーバーの `db_path` を第3引数で指定可能（デフォルト: `pobox_history.db`）
  - `Search` RPC 時に `history_.load_boost_map()` でブースト値を取得
  - `Learn` RPC 時に `history_.record(id, prefix)` で選択を記録

- [x] `src/client/main.cc` — Learn コマンド追加
  - `pobox_client learn <candidate_id> <prefix>` で Learn RPC を呼び出し可能

#### 動作確認済み（2026-04-09）

```bash
# 1回目: 候補検索
./build/pobox_client search "あい"
# → 愛 [あい] score=0.95

# 学習記録
./build/pobox_client learn "あい:愛" "あい"
# Learned: あい:愛 (prefix: あい)

# 2回目: サーバー再起動後も score が上昇
./build/pobox_server dict/default.tsv pobox_history.db  # 再起動
./build/pobox_client search "あい"
# → 愛 [あい] score=1.01931  ✅ (boost ≈ 0.0693)

# 複数回学習テスト: さらに2回学習
./build/pobox_client learn "あい:愛" "あい"
./build/pobox_client learn "あい:愛" "あい"
./build/pobox_server dict/default.tsv pobox_history.db  # 再起動
./build/pobox_client search "あい"
# → 愛 [あい] score=1.08863  ✅ (boost ≈ 0.1386)
```

**計算式検証:**
- 1回学習: `log(1 + 1) × 0.1 = log(2) × 0.1 ≈ 0.0693` ✅
- 3回学習: `log(1 + 3) × 0.1 = log(4) × 0.1 ≈ 0.1386` ✅

実装の数学的正確性を確認。

---

### Phase 4 — テスト (CPPUnit)（完了 + 拡充 ✅）

- [x] `tests/test_runner.cc` — CppUnit エントリポイント
- [x] `tests/test_dictionary.cc` — 前方一致・空辞書・不正行・例外など 9件
- [x] `tests/test_scorer.cc` — boost 計算・apply・ソート順など 7件
- [x] `tests/test_learn.cc` — History テスト 6件（`:memory:` DB 使用）
  - 空の boost_map テスト
  - 1回記録でカウント +1
  - 複数回記録でカウント累積
  - 複数の異なる候補を個別に管理
  - 未記録候補は map に含まれない
  - 空プリフィックスでも記録可能

```bash
ctest --test-dir build
# → 100% tests passed, 0 tests failed out of 1
# テスト総数: 22件
```

---

### Phase 5 — 同期機能（将来）

**目標:** Sync RPC で複数端末の学習データをマージする。

POBox 原典の「別端末で学習した内容を同期」に対応する。

#### 想定実装

- サーバー側:
  - `last_sync_timestamp` 以降の `score_override` 差分を返す
  - クライアント個別の同期チェーンを追跡（オプション）

- クライアント側:
  - Sync RPC で差分を取得
  - ローカル SQLite にマージ

- 競合解決:
  - Last-Write-Wins（タイムスタンプが新しい方を優先）

#### 依存関係

- Phase 3 が完全に安定した後に着手

---

## 優先順位と依存関係

```
Phase 1 ✅ → Phase 2 ✅ → Phase 3 ✅ → Phase 4 ✅ → Phase 5（次）
```

### コミット履歴

- `4442b77` - implement Phase 3: SQLite-based learning persistence
- `198b748` - enhance client: add Learn RPC support for testing
