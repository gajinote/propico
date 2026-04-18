# pobox-neo

## POBox を C++ で現代（2026）風に再実装

**pobox-neo** は、1996年にソニーCSL の増井俊之によって開発された予測式日本語入力エンジン「POBox」を、最新の C++20 で再実装したプロジェクトです。

POBox の根幹にある設計思想 ― **品詞解析を捨てた軽量エンジン × 使用頻度ベースの動的学習 × 低リソース環境への徹底特化** ― を現代の C++ と gRPC で再現することを目指します。

---

## 実装状況

| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 1 | インフラ整備（gRPC / CMake） | ✅ 完了 |
| Phase 2 | 辞書エンジン + 予測（Search RPC） | ✅ 完了 |
| Phase 3 | 学習機能（SQLite 永続化 + Learn RPC） | ✅ 完了 |
| Phase 4 | テスト（CPPUnit, 28 ケース） | ✅ 完了 |
| Phase 5 | 同期機能（Sync RPC） | ✅ 完了 |

**テスト:** 28/28 ✅  
**コマンド:** `search`, `learn`, `sync` 全実装

---

## アーキテクチャ概要

POBox のアーキテクチャは、フロントエンド予測とバックエンド変換の **責務分離** が特徴です。

```
クライアント (CLI)                サーバー (gRPC エンジン)
┌─────────────────────┐            ┌───────────────────────────────┐
│ pobox_client        │ gRPC       │  変換エンジン                  │
│                     │            │ ┌──────────┐ ┌──────────────┐ │
│ ・Search (prefix)   │ ─────────▶ │ │ 辞書検索 │ │ 学習履歴     │ │
│ ・Learn (id)        │ ◀────────  │ │(前方一致)│ │ (SQLite DB) │ │
│ ・Sync (timestamp)  │ ─────────▶ │ │          │ │              │ │
└─────────────────────┘            │ └──────────┘ └──────────────┘ │
                                    │ ・boost 計算                  │
                                    │ ・スコア統合・ソート         │
                                    │ ・差分同期                   │
                                    └───────────────────────────────┘
```

### エンジンの設計原則

| 原則 | 内容 |
|---|---|
| 品詞解析なし | 品詞情報を一切使わないため、日本語・英語・プログラミング言語に汎用適用できる |
| 2軸予測 | 辞書ベースの予測（登録語との前方一致）と履歴ベースの予測（過去の選択）を組み合わせる |
| 動的ランキング | 選択頻度が高い候補が上位に昇格し、使えば使うほど効率が上がる |
| 最小辞書設計 | デフォルト辞書は意図的に絞り込み、ドメイン特化辞書を追加できる拡張構造にする |

### gRPC サービス定義（`pobox.proto`）

| RPC | 用途 | 実装 |
|---|---|---|
| `Search(prefix)` | 入力プレフィックスから候補リストを返す | ✅ |
| `Learn(candidate_id, prefix)` | 選択された候補を学習し優先度を上げる（SQLite 記録） | ✅ |
| `Sync(last_sync_timestamp)` | サーバー側の更新差分を返す（複数端末同期用） | ✅ |

---

## ディレクトリ構成

```
pobox-neo/
├── CMakeLists.txt
├── pobox.proto              ← gRPC サービス定義
├── dict/
│   └── default.tsv          ← デフォルト辞書（読み\t表記\tスコア）
├── src/
│   ├── server/main.cc       ← gRPC サーバー（変換エンジン）
│   ├── client/main.cc       ← CLI クライアント（テスト用）
│   └── engine/
│       ├── candidate.h      ← Candidate 内部表現
│       ├── dictionary.h/.cc ← TSV 辞書の読み込み・前方一致検索
│       ├── scorer.h/.cc     ← 辞書スコア + 学習 boost の統合
│       └── history.h/.cc    ← SQLite ラッパー（学習履歴・同期）
└── tests/                   ← CPPUnit テスト（28 ケース）
    ├── test_runner.cc
    ├── test_dictionary.cc   (9 ケース)
    ├── test_scorer.cc       (7 ケース)
    ├── test_learn.cc        (6 ケース)
    └── test_sync.cc         (6 ケース)
```

### エンジンコンポーネント詳細

#### `engine/candidate.h` — 内部 Candidate 型

`pobox::engine::Candidate` は辞書検索からスコア確定までの内部表現です。
proto の `pobox::Candidate` とは独立しており、サーバーが RPC レスポンスを構築する際に変換します。

```cpp
struct Candidate {
    std::string id;         // "reading:text" 形式の安定識別子（Learn で使用）
    std::string text;       // 表示テキスト（漢字・かな交じり文など）
    std::string reading;    // 読み（ひらがな）
    float       base_score; // 辞書由来の基本スコア（0.0〜1.0）
    float       boost;      // 学習による加算分
    
    float final_score() const { return base_score + boost; }
};
```

#### `engine/dictionary.h/.cc` — TSV 辞書

起動時に TSV ファイルを読み込み、`reading` でソートして保持します。
検索は `std::ranges::lower_bound` による前方一致で O(log N + 結果数) です。

```
辞書フォーマット（1行）: reading TAB text TAB base_score
例: あい\t愛\t0.95
    あい\t相手\t0.88
```

#### `engine/scorer.h/.cc` — スコア統合

```
final_score = base_score + boost
boost       = log(1 + 選択回数) × decay_factor  (デフォルト: 0.1)
```

辞書検索結果に `history_` から取得した boost を適用し、`final_score` 降順で並べ替えて返します。

#### `engine/history.h/.cc` — SQLite 学習・同期

```cpp
class History {
    // 選択を記録（learn_log テーブルに追記、score_override のカウントを +1）
    void record(const std::string& candidate_id, const std::string& prefix);
    
    // 全候補の選択カウント取得（Search RPC で使用）
    std::unordered_map<std::string, int> load_boost_map() const;
    
    // since タイムスタンプ以降の更新差分を返す（Sync RPC で使用）
    std::vector<SyncRow> load_updated_since(int64_t since) const;
    
    // リモートから受け取った差分をマージ（LWW: Last-Write-Wins）
    void merge_entries(const std::vector<SyncRow>& entries);
};
```

**スキーマ:**

```sql
-- 選択履歴（生ログ）
CREATE TABLE learn_log (
    candidate_id TEXT NOT NULL,
    prefix       TEXT NOT NULL,
    selected_at  INTEGER NOT NULL  -- Unix 秒
);

-- スコアカウント（集計済みキャッシュ）
CREATE TABLE score_override (
    candidate_id TEXT    PRIMARY KEY,
    count        INTEGER NOT NULL DEFAULT 1,
    updated_at   INTEGER NOT NULL
);
```

---

## ビルド手順

### 依存パッケージ (Ubuntu / Debian)

```bash
sudo apt install -y \
  libprotobuf-dev \
  protobuf-compiler \
  libgrpc++-dev \
  libgrpc-dev \
  protobuf-compiler-grpc \
  libsqlite3-dev \
  libcppunit-dev
```

| パッケージ | 役割 |
|---|---|
| `libprotobuf-dev` | Protocol Buffers C++ ライブラリ・ヘッダ |
| `protobuf-compiler` | `protoc` コマンド（`.proto` → C++ 変換） |
| `libgrpc++-dev` | gRPC C++ ライブラリ・ヘッダ |
| `libgrpc-dev` | gRPC コアライブラリ |
| `protobuf-compiler-grpc` | `grpc_cpp_plugin`（gRPC スタブ生成） |
| `libsqlite3-dev` | SQLite3 C ライブラリ |
| `libcppunit-dev` | CPPUnit テストフレームワーク |

### ビルド

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### テスト実行

```bash
ctest --test-dir build --output-on-failure
# → 100% tests passed, 28 tests passed
```

---

## 使用例

### 1. サーバー起動

```bash
./build/pobox_server dict/default.tsv pobox_history.db
# → PoBox server listening on 0.0.0.0:50051
# → Dictionary loaded: 190 entries
# → History database: pobox_history.db
```

### 2. 検索（Search RPC）

```bash
./build/pobox_client search "あい"
# → 愛  [あい]  score=0.95
# → 挨拶  [あいさつ]  score=0.9
# → 相手  [あいて]  score=0.88
# → ...
```

### 3. 学習記録（Learn RPC）

```bash
./build/pobox_client learn "あい:愛" "あい"
# → Learned: あい:愛 (prefix: あい)
```

サーバーを再起動してから再検索すると、「愛」のスコアが上昇します。

```bash
# サーバー再起動（SQLite データベースは保持）
./build/pobox_server dict/default.tsv pobox_history.db

./build/pobox_client search "あい"
# → 愛  [あい]  score=1.01931  ← boost が加算されている
# → 挨拶  [あいさつ]  score=0.9
# → ...
```

### 4. 同期（Sync RPC）

複数端末の学習データを同期します。

```bash
# クライアント1: 全データ取得（初期同期）
./build/pobox_client sync 0
# → server_timestamp=1775890026
# → entries=0

# クライアント1: 学習記録
./build/pobox_client learn "あい:愛" "あい"

# クライアント2: 差分取得
./build/pobox_client sync 1775890026
# → server_timestamp=1775890027
# → entries=0  ← 差分なし（前回のタイムスタンプより新しい Learn がない）

# クライアント1: さらに学習
./build/pobox_client learn "あい:愛" "あい"

# クライアント2: 差分確認
./build/pobox_client sync 1775890026
# → server_timestamp=1775890028
# → entries=1
# →   あい:愛  count=2  updated_at=1775890027
```

---

## 設計上の特性

### 軽量性
- 品詞解析なし → 自然言語処理の重いライブラリ不要
- gRPC で通信 → バイナリプロトコル、低レイテンシ

### スケーラビリティ
- 辞書検索は O(log N + 結果数)
- 学習カウントは集計済み状態で保存（即座に boost 計算可能）
- SQLite は単一ファイルベース → 簡単に複製・バックアップ可能

### 拡張性
- Sync RPC で複数端末間の学習同期
- 異なるドメインの辞書を容易に追加可能
- Candidate 構造で内部とプロトコルを分離

---

## ライセンス・参考

本プロジェクトは POBox の設計思想を再実装したものです。  
POBox の原論文: [増井俊之, "An Efficient Text Input Method for Handheld and Ubiquitous Computers"](http://www.pitecan.com/papers/HUC99/HUC99.pdf)

---

## 関連リソース

- [PLAN.md](PLAN.md) — 実装計画（フェーズ別の詳細）
- [POBOX-ARCH.md](POBOX-ARCH.md) — 設計ドキュメント（理論的背景）
- [pobox.proto](pobox.proto) — gRPC サービス定義
