cmake_minimum_required(VERSION 3.24)
project(Propico VERSION 0.1.0 LANGUAGES CXX)

# -----------------------------------------------------------------------
# 言語標準
# -----------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------
# 依存パッケージ
# -----------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)   # grpc++ を含む
find_package(SQLite3 REQUIRED)

# -----------------------------------------------------------------------
# Protocol Buffers / gRPC コード生成
# -----------------------------------------------------------------------
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/propico.proto")
set(PROTO_OUT  "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY "${PROTO_OUT}")

# protoc プラグインのパスを取得
get_target_property(PROTOC_EXE    protobuf::protoc    IMPORTED_LOCATION_RELEASE)
get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)

# RELEASE が空のときは DEBUG 版にフォールバック
if(NOT PROTOC_EXE)
    get_target_property(PROTOC_EXE    protobuf::protoc    IMPORTED_LOCATION_DEBUG)
endif()
if(NOT GRPC_CPP_PLUGIN)
    get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_DEBUG)
endif()
# それでも見つからなければ PATH から探す
if(NOT PROTOC_EXE)
    find_program(PROTOC_EXE protoc REQUIRED)
endif()
if(NOT GRPC_CPP_PLUGIN)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
endif()

set(PROTO_SRCS
    "${PROTO_OUT}/propico.pb.cc"
    "${PROTO_OUT}/propico.grpc.pb.cc"
)
set(PROTO_HDRS
    "${PROTO_OUT}/propico.pb.h"
    "${PROTO_OUT}/propico.grpc.pb.h"
)

add_custom_command(
    OUTPUT  ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND "${PROTOC_EXE}"
            "--proto_path=${CMAKE_CURRENT_SOURCE_DIR}"
            "--cpp_out=${PROTO_OUT}"
            "--grpc_out=${PROTO_OUT}"
            "--plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}"
            "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
    COMMENT "Generating C++ from propico.proto"
    VERBATIM
)

# 生成コードをまとめたライブラリ
add_library(propico_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(propico_proto PUBLIC "${PROTO_OUT}")
target_link_libraries(propico_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)

# -----------------------------------------------------------------------
# 予測エンジン
# -----------------------------------------------------------------------
add_library(propico_engine STATIC
    src/engine/dictionary.cc
    src/engine/scorer.cc
    src/engine/history.cc
)
target_include_directories(propico_engine PUBLIC src/engine)
target_link_libraries(propico_engine PUBLIC propico_proto SQLite::SQLite3)

# -----------------------------------------------------------------------
# サーバー
# -----------------------------------------------------------------------
add_executable(propico_server
    src/server/main.cc
)
target_link_libraries(propico_server
    PRIVATE
        propico_engine
        Threads::Threads
)

# -----------------------------------------------------------------------
# クライアント
# -----------------------------------------------------------------------
add_executable(propico_client
    src/client/main.cc
)
target_link_libraries(propico_client
    PRIVATE
        propico_proto
        Threads::Threads
)

# -----------------------------------------------------------------------
# テスト (CPPUnit)
# -----------------------------------------------------------------------
find_package(CppUnit QUIET)

if(CppUnit_FOUND OR TARGET CppUnit::CppUnit)
    enable_testing()

    file(GLOB_RECURSE TEST_SRCS "tests/*.cc")
    if(TEST_SRCS)
        add_executable(propico_tests ${TEST_SRCS})
        target_link_libraries(propico_tests
            PRIVATE
                propico_engine
                CppUnit::CppUnit
                Threads::Threads
        )
        add_test(NAME propico_tests COMMAND propico_tests)
    endif()
else()
    # pkg-config 経由でも探す
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CPPUNIT cppunit)
    endif()

    if(CPPUNIT_FOUND)
        enable_testing()

        file(GLOB_RECURSE TEST_SRCS "tests/*.cc")
        if(TEST_SRCS)
            add_executable(propico_tests ${TEST_SRCS})
            target_include_directories(propico_tests PRIVATE ${CPPUNIT_INCLUDE_DIRS})
            target_link_libraries(propico_tests
                PRIVATE
                    propico_engine
                    ${CPPUNIT_LIBRARIES}
                    Threads::Threads
            )
            add_test(NAME propico_tests COMMAND propico_tests)
        endif()
    else()
        message(STATUS "CppUnit not found — test target skipped")
    endif()
endif()
