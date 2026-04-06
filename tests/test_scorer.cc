#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <cmath>
#include <unordered_map>

#include "scorer.h"

static constexpr float kEps = 1e-5f;

class ScorerTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ScorerTest);
    CPPUNIT_TEST(test_boost_zero_count);
    CPPUNIT_TEST(test_boost_positive_count);
    CPPUNIT_TEST(test_boost_formula);
    CPPUNIT_TEST(test_apply_no_boost);
    CPPUNIT_TEST(test_apply_with_boost);
    CPPUNIT_TEST(test_apply_sorted_descending);
    CPPUNIT_TEST(test_apply_unknown_id_ignored);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override {
        scorer_ = pobox::engine::Scorer(/*decay_factor=*/0.1f);
    }

    // 選択回数 0 → boost は 0
    void test_boost_zero_count() {
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0f, scorer_.compute_boost(0), kEps);
    }

    // 選択回数が正の値 → boost は正
    void test_boost_positive_count() {
        CPPUNIT_ASSERT(scorer_.compute_boost(1) > 0.0f);
    }

    // boost = log(1 + count) * decay_factor
    void test_boost_formula() {
        const float expected = std::log1p(5.0f) * 0.1f;
        CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, scorer_.compute_boost(5), kEps);
    }

    // boost_map が空のとき final_score == base_score
    void test_apply_no_boost() {
        std::vector<pobox::engine::Candidate> candidates = {make_candidate("a:X", 0.8f)};
        std::unordered_map<std::string, int>  boost_map;

        auto result = scorer_.apply(candidates, boost_map);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.8f, result[0].final_score(), kEps);
    }

    // boost_map にある候補は final_score が上昇する
    void test_apply_with_boost() {
        std::vector<pobox::engine::Candidate> candidates = {make_candidate("a:X", 0.5f)};
        std::unordered_map<std::string, int>  boost_map  = {{"a:X", 10}};

        auto result = scorer_.apply(candidates, boost_map);
        CPPUNIT_ASSERT(result[0].final_score() > 0.5f);
    }

    // 結果は final_score 降順
    void test_apply_sorted_descending() {
        std::vector<pobox::engine::Candidate> candidates = {
            make_candidate("a:A", 0.5f),
            make_candidate("a:B", 0.9f),
            make_candidate("a:C", 0.7f),
        };
        std::unordered_map<std::string, int> boost_map;

        auto result = scorer_.apply(candidates, boost_map);
        for (std::size_t i = 1; i < result.size(); ++i) {
            CPPUNIT_ASSERT(result[i - 1].final_score() >= result[i].final_score());
        }
    }

    // boost_map に存在しない id は boost = 0 のまま
    void test_apply_unknown_id_ignored() {
        std::vector<pobox::engine::Candidate> candidates = {make_candidate("a:X", 0.8f)};
        std::unordered_map<std::string, int>  boost_map  = {{"unknown:Y", 99}};

        auto result = scorer_.apply(candidates, boost_map);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.8f, result[0].final_score(), kEps);
    }

private:
    static pobox::engine::Candidate make_candidate(const std::string& id, float base_score) {
        pobox::engine::Candidate c;
        c.id         = id;
        c.text       = id;
        c.reading    = id;
        c.base_score = base_score;
        c.boost      = 0.0f;
        return c;
    }

    pobox::engine::Scorer scorer_;
};

CPPUNIT_TEST_SUITE_REGISTRATION(ScorerTest);
