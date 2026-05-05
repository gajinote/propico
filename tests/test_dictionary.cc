#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <filesystem>
#include <fstream>

#include "dictionary.h"

namespace fs = std::filesystem;

// テスト用 TSV を一時ファイルに書き出すヘルパー
static fs::path write_temp_tsv(const std::string& content) {
    auto path = fs::temp_directory_path() / "propico_test_dict.tsv";
    std::ofstream ofs(path);
    ofs << content;
    return path;
}

class DictionaryTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(DictionaryTest);
    CPPUNIT_TEST(test_load_and_size);
    CPPUNIT_TEST(test_prefix_match_exact);
    CPPUNIT_TEST(test_prefix_match_partial);
    CPPUNIT_TEST(test_no_match);
    CPPUNIT_TEST(test_empty_prefix_returns_empty);
    CPPUNIT_TEST(test_score_descending_order);
    CPPUNIT_TEST(test_max_results_limit);
    CPPUNIT_TEST(test_skip_comment_and_blank_lines);
    CPPUNIT_TEST(test_load_nonexistent_throws);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override {
        const std::string tsv =
            "# comment\n"
            "\n"
            "あい\t愛\t0.95\n"
            "あい\t間\t0.80\n"
            "あい\t藍\t0.60\n"
            "あいさつ\t挨拶\t0.90\n"
            "あいて\t相手\t0.88\n"
            "いぬ\t犬\t0.92\n"
            "いぬ\t戌\t0.70\n";
        dict_path_ = write_temp_tsv(tsv);
        dict_.load(dict_path_);
    }

    void tearDown() override {
        fs::remove(dict_path_);
    }

    // エントリ数が正しく読み込まれる
    void test_load_and_size() {
        CPPUNIT_ASSERT_EQUAL(std::size_t(7), dict_.size());
    }

    // 完全一致のプレフィックスで全候補が返る
    void test_prefix_match_exact() {
        auto results = dict_.search("あい");
        CPPUNIT_ASSERT_EQUAL(std::size_t(5), results.size());
    }

    // 部分プレフィックスで前方一致する
    void test_prefix_match_partial() {
        auto results = dict_.search("あいさ");
        CPPUNIT_ASSERT_EQUAL(std::size_t(1), results.size());
        CPPUNIT_ASSERT_EQUAL(std::string("挨拶"), results[0].text);
    }

    // マッチなし
    void test_no_match() {
        auto results = dict_.search("zzz");
        CPPUNIT_ASSERT(results.empty());
    }

    // 空プレフィックスは空を返す
    void test_empty_prefix_returns_empty() {
        auto results = dict_.search("");
        CPPUNIT_ASSERT(results.empty());
    }

    // 結果はスコア降順
    void test_score_descending_order() {
        auto results = dict_.search("あい");
        for (std::size_t i = 1; i < results.size(); ++i) {
            CPPUNIT_ASSERT(results[i - 1].final_score() >= results[i].final_score());
        }
    }

    // max_results で件数が絞られる
    void test_max_results_limit() {
        auto results = dict_.search("あい", 2);
        CPPUNIT_ASSERT_EQUAL(std::size_t(2), results.size());
    }

    // コメント行・空行はスキップされる
    void test_skip_comment_and_blank_lines() {
        // setUp で読み込んだ辞書にコメント・空行が含まれるが size は 7
        CPPUNIT_ASSERT_EQUAL(std::size_t(7), dict_.size());
    }

    // 存在しないファイルは例外
    void test_load_nonexistent_throws() {
        propico::engine::Dictionary d;
        CPPUNIT_ASSERT_THROW(d.load("/nonexistent/path.tsv"), std::runtime_error);
    }

private:
    fs::path                  dict_path_;
    propico::engine::Dictionary dict_;
};

CPPUNIT_TEST_SUITE_REGISTRATION(DictionaryTest);
