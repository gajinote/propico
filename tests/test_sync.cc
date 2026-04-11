#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <ctime>
#include <sqlite3.h>

#include "history.h"

class SyncTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SyncTest);
    CPPUNIT_TEST(test_load_since_empty);
    CPPUNIT_TEST(test_load_since_zero_returns_all);
    CPPUNIT_TEST(test_load_since_with_records);
    CPPUNIT_TEST(test_merge_insert_new);
    CPPUNIT_TEST(test_merge_multiple_entries);
    CPPUNIT_TEST(test_merge_replaces_existing_with_newer_timestamp);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override {
        history_ = std::make_unique<pobox::engine::History>(":memory:");
    }

    void tearDown() override {
        history_.reset();
    }

    // 何も記録しなければ load_updated_since は空 vector を返す
    void test_load_since_empty() {
        auto rows = history_->load_updated_since(0);
        CPPUNIT_ASSERT(rows.empty());
    }

    // since=0 で全件取得
    void test_load_since_zero_returns_all() {
        history_->record("id1:text1", "prefix1");
        history_->record("id2:text2", "prefix2");
        auto rows = history_->load_updated_since(0);
        CPPUNIT_ASSERT_EQUAL(2, (int)rows.size());
    }

    // since > 0 でも同じ秒内のレコードは取得される
    void test_load_since_with_records() {
        history_->record("id1:text1", "prefix1");
        auto all_rows = history_->load_updated_since(0);
        CPPUNIT_ASSERT_EQUAL(1, (int)all_rows.size());

        // get current timestamp
        const std::int64_t current_time = all_rows[0].updated_at;

        // since > current_time で新規記録はない（using future timestamp）
        auto rows = history_->load_updated_since(current_time + 86400);  // +1日
        CPPUNIT_ASSERT(rows.empty());
    }

    // merge で新規エントリが挿入される
    void test_merge_insert_new() {
        std::vector<pobox::engine::SyncRow> entries = {
            {"id1:text1", 5, 1000},
        };
        history_->merge_entries(entries);

        auto boost_map = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(1, (int)boost_map.size());
        CPPUNIT_ASSERT_EQUAL(5, boost_map.at("id1:text1"));
    }

    // merge で複数エントリが挿入される
    void test_merge_multiple_entries() {
        std::vector<pobox::engine::SyncRow> entries = {
            {"id1:text1", 5, 1000},
            {"id2:text2", 3, 1001},
            {"id3:text3", 7, 999},
        };
        history_->merge_entries(entries);

        auto boost_map = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(3, (int)boost_map.size());
        CPPUNIT_ASSERT_EQUAL(5, boost_map.at("id1:text1"));
        CPPUNIT_ASSERT_EQUAL(3, boost_map.at("id2:text2"));
        CPPUNIT_ASSERT_EQUAL(7, boost_map.at("id3:text3"));
    }

    // merge で既存エントリが新しいタイムスタンプで置き換わる
    void test_merge_replaces_existing_with_newer_timestamp() {
        // ローカル記録
        history_->record("shared:id", "prefix");
        auto local_boost = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(1, local_boost.at("shared:id"));

        // get current time of local record
        auto local_rows = history_->load_updated_since(0);
        const std::int64_t local_time = local_rows[0].updated_at;

        // リモートから newer timestamp で merge
        std::vector<pobox::engine::SyncRow> remote_entries = {
            {"shared:id", 10, local_time + 1000},  // より新しいタイムスタンプ
        };
        history_->merge_entries(remote_entries);

        auto updated_boost = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(10, updated_boost.at("shared:id"));
    }

private:
    std::unique_ptr<pobox::engine::History> history_;
};

CPPUNIT_TEST_SUITE_REGISTRATION(SyncTest);
