# pobox-neo

## POBox を C++ で現代（2026）風に再実装

**pobox-neo** は、1996年にソニーCSL の増井俊之によって開発された予測式日本語入力エンジン「POBox」を、最新の C++20 で再実装したプロジェクトです。

POBox の根幹にある設計思想 ― **品詞解析を捨てた軽量エンジン × 使用頻度ベースの動的学習 × 低リソース環境への徹底特化** ― を現代の C++ と gRPC で再現することを目指します。

---

## アーキテクチャ概要

POBox のアーキテクチャは、フロントエンド予測とバックエンド変換の **責務分離** が特徴です。

```
クライアント (UI)                  サーバー (変換エンジン)
┌─────────────────────┐  gRPC      ┌──────────────────────────────┐
│ pobox_client        │ ─Search──▶ │  予測候補生成                 │
│                     │ ◀─候補──── │  ┌──────────┐ ┌───────────┐  │
│ ・入力文字列を送信   │            │  │ 辞書検索 │ │ 入力履歴  │  │
│ ・候補リストを表示   │ ─Learn───▶ │  │(曖昧一致)│ │ (動的学習)│  │
│ ・選択候補を学習登録 │            │  └──────────┘ └───────────┘  │
└─────────────────────┘            └──────────────────────────────┘
```

### エンジンの設計原則

| 原則 | 内容 |
|---|---|
| 品詞解析なし | 品詞情報を一切使わないため、日本語・英語・プログラミング言語に汎用適用できる |
| 2軸予測 | 辞書ベースの予測（登録語との前方一致）と履歴ベースの予測（過去の選択）を組み合わせる |
| 動的ランキング | 選択頻度が高い候補が上位に昇格し、使えば使うほど効率が上がる |
| 最小辞書設計 | デフォルト辞書は意図的に絞り込み、ドメイン特化辞書を追加できる拡張構造にする |

### gRPC サービス定義（`pobox.proto`）

| RPC | 用途 |
|---|---|
| `Search(prefix)` | 入力プレフィックスから候補リストを返す |
| `Learn(candidate_id)` | 選択された候補を学習し優先度を上げる |
| `Sync()` | 複数端末間で学習データを同期する（将来用） |

---

## ディレクトリ構成

```
pobox-neo/
├── CMakeLists.txt
├── pobox.proto
├── dict/
│   └── default.tsv          ← デフォルト辞書（読み\t表記\t基本スコア）
├── src/
│   ├── server/main.cc       ← gRPC サーバー（変換エンジン）
│   ├── client/main.cc       ← CLI クライアント（テスト用）
│   └── engine/
│       ├── candidate.h      ← Candidate 内部表現（id/text/reading/score）
│       ├── dictionary.h/.cc ← TSV 辞書の読み込み・前方一致検索
│       └── scorer.h/.cc     ← 辞書スコア + 学習 boost の統合
└── tests/                   ← CPPUnit テスト
```

### エンジンコンポーネント詳細

#### `engine/candidate.h` — 内部 Candidate 型

`pobox::engine::Candidate` は辞書検索からスコア確定までの内部表現です。
proto の `pobox::Candidate` とは独立しており、サーバーが RPC レスポンスを構築する際に変換します。

```
id         : "reading:text" 形式の安定識別子（Learn RPC で使用）
text       : 表示テキスト（漢字・かな交じり文など）
reading    : 読み（ひらがな）
base_score : 辞書由来の基本スコア（0.0〜1.0）
boost      : 学習による加算分（Phase 3 で SQLite から設定）
final_score: base_score + boost
```

#### `engine/dictionary.h/.cc` — TSV 辞書

起動時に TSV ファイルを読み込み、`reading` でソートして保持します。
検索は `std::ranges::lower_bound` による前方一致で O(log N + 結果数) です。

```
辞書フォーマット（1行）: reading TAB text TAB base_score
例: あい\t愛\t0.95
```

#### `engine/scorer.h/.cc` — スコア統合

```
final_score = base_score + boost
boost       = log(1 + 選択回数) × decay_factor  (デフォルト: 0.1)
```

辞書検索結果に boost を適用し、`final_score` 降順で並べ替えて返します。

---

## ビルド手順

### 依存パッケージ (Ubuntu / Debian)

```bash
sudo apt install -y \
  libprotobuf-dev \
  protobuf-compiler \
  libgrpc++-dev \
  libgrpc-dev \
  protobuf-compiler-grpc
```

| パッケージ | 役割 |
|---|---|
| `libprotobuf-dev` | Protocol Buffers C++ ライブラリ・ヘッダ |
| `protobuf-compiler` | `protoc` コマンド（`.proto` → C++ 変換） |
| `libgrpc++-dev` | gRPC C++ ライブラリ・ヘッダ |
| `libgrpc-dev` | gRPC コアライブラリ |
| `protobuf-compiler-grpc` | `grpc_cpp_plugin`（gRPC スタブ生成） |

### ビルド

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 実行例

```bash
# サーバー起動
./build/pobox_server

# クライアントから検索
./build/pobox_client "あい"
```
