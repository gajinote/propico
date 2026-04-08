#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include "history.h"

class HistoryTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(HistoryTest);
    CPPUNIT_TEST(test_empty_boost_map);
    CPPUNIT_TEST(test_record_increments_count);
    CPPUNIT_TEST(test_record_multiple_times);
    CPPUNIT_TEST(test_record_multiple_candidates);
    CPPUNIT_TEST(test_unknown_candidate_not_in_map);
    CPPUNIT_TEST(test_record_with_empty_prefix);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override {
        // ":memory:" を使うことでファイルを作らずテスト実行
        history_ = std::make_unique<pobox::engine::History>(":memory:");
    }

    void tearDown() override {
        history_.reset();
    }

    // 何も記録しなければ boost_map は空
    void test_empty_boost_map() {
        auto map = history_->load_boost_map();
        CPPUNIT_ASSERT(map.empty());
    }

    // 1 回記録すると count == 1
    void test_record_increments_count() {
        history_->record("あい:愛", "あい");
        auto map = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(1, map.at("あい:愛"));
    }

    // 同じ候補を N 回記録すると count == N
    void test_record_multiple_times() {
        history_->record("あい:愛", "あい");
        history_->record("あい:愛", "あい");
        history_->record("あい:愛", "あい");
        auto map = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(3, map.at("あい:愛"));
    }

    // 複数の異なる候補を個別にカウント
    void test_record_multiple_candidates() {
        history_->record("あい:愛", "あい");
        history_->record("あい:愛", "あい");
        history_->record("あい:相", "あい");
        auto map = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(2, map.at("あい:愛"));
        CPPUNIT_ASSERT_EQUAL(1, map.at("あい:相"));
    }

    // 記録していない候補は boost_map に含まれない
    void test_unknown_candidate_not_in_map() {
        history_->record("あい:愛", "あい");
        auto map = history_->load_boost_map();
        CPPUNIT_ASSERT(map.find("ない:候補") == map.end());
    }

    // prefix が空でも記録できる
    void test_record_with_empty_prefix() {
        history_->record("あ:亜", "");
        auto map = history_->load_boost_map();
        CPPUNIT_ASSERT_EQUAL(1, map.at("あ:亜"));
    }

private:
    std::unique_ptr<pobox::engine::History> history_;
};

CPPUNIT_TEST_SUITE_REGISTRATION(HistoryTest);
