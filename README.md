# pobox-neo

## POBoxをC++で現代(2026)風に再実装

**pobox-neo**は、1990年代後半に開発された予測式日本語入力エンジン「POBox」を、最新のC++（2026）で再実装したものです。その目的は、POBoxの核心となる理念（軽量で予測ベースのテキスト入力）を、現代のC++標準に適合させることにあります。

## ビルド、テスト、およびリンティング

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

### ビルド手順

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## アーキテクチャ
プロジェクトは初期段階にあります。コードベースが拡大するにつれ、予測エンジンの動作、候補ランキングに使用されるデータ構造、入力シーケンスが候補にどのようにマッピングされるかなど、アーキテクチャ上の決定事項をここに記録してください。

1. 通信プロトコルの定義 (IDLの作成)
まず着手すべきは「サーバーとクライアントの間でどんなやり取りをするか」の定義です。
クロスプラットフォームを前提にするなら、gRPC (Protocol Buffers) を強く推奨します。

理由: C++はもちろん、Java(Android), Swift(iOS/macOS), Pythonなど、ほぼ全ての言語でコードを自動生成でき、型安全な通信が可能です。

定義すべきメソッド例:

Search(prefix): 入力中の文字列から候補を返す。

Learn(candidate_id): 選択された候補を学習し、優先度を上げる。

Sync(): 別の端末で学習した内容を同期する（将来用）。

## ディレクトリ構成
pobox-neo/
├── CMakeLists.txt
├── pobox.proto
├── src/
│   ├── server/main.cc   ← 次に作成するファイル
│   └── client/main.cc   ← 次に作成するファイル
└── tests/
    └── *.cc             ← CPPUnit テスト