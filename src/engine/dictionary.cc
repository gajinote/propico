#include "dictionary.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <ranges>
#include <array>
#include <stdexcept>
#include <string_view>

namespace propico::engine {

namespace {

// TSV の1行を分解して Candidate を構築する。
// 形式: reading TAB text TAB base_score
// 不正な行は std::nullopt を返す
std::optional<Candidate> parse_line(std::string_view line) {
    // 空行・コメント行はスキップ
    if (line.empty() || line.starts_with('#')) return std::nullopt;

    auto split_next = [](std::string_view& sv) -> std::optional<std::string_view> {
        auto pos = sv.find('\t');
        if (pos == std::string_view::npos) return std::nullopt;
        auto field = sv.substr(0, pos);
        sv.remove_prefix(pos + 1);
        return field;
    };

    auto reading_sv = split_next(line);
    auto text_sv    = split_next(line);
    if (!reading_sv || !text_sv || line.empty()) return std::nullopt;

    float score = 0.5f;
    const auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), score);
    if (ec != std::errc{}) return std::nullopt;

    Candidate c;
    c.reading    = std::string(*reading_sv);
    c.text       = std::string(*text_sv);
    c.id         = c.reading + ':' + c.text;
    c.base_score = score;
    c.boost      = 0.0f;
    return c;
}

// 末尾で分離する助詞（長いものを先に判定する）
constexpr std::array<std::string_view, 12> kParticles = {
    "から", "まで", "より", "は", "が", "を", "に", "の", "で", "と", "も", "へ",
};

// 複合候補は単独の語より少し下げる
constexpr float kParticleScoreFactor = 0.9f;

} // namespace

void Dictionary::load(const std::filesystem::path& tsv_path) {
    std::ifstream ifs(tsv_path);
    if (!ifs) {
        throw std::runtime_error("Dictionary: cannot open " + tsv_path.string());
    }

    entries_.clear();
    std::string line;
    while (std::getline(ifs, line)) {
        if (auto c = parse_line(line)) {
            entries_.push_back(std::move(*c));
        }
    }

    // reading の昇順にソート（binary search のため）
    std::ranges::stable_sort(entries_, {}, &Candidate::reading);
}

std::vector<Candidate> Dictionary::search(const std::string& prefix,
                                          std::size_t        max_results) const {
    if (prefix.empty()) return {};

    // prefix 以上の最初のエントリを binary search で探す
    auto it = std::ranges::lower_bound(entries_, prefix, {}, &Candidate::reading);

    // 件数制限は並べ替えの後に掛ける（読み順の先頭だけが残るのを避ける）
    std::vector<Candidate> result;
    for (; it != entries_.end(); ++it) {
        if (!it->reading.starts_with(prefix)) break;
        result.push_back(*it);
    }

    std::ranges::stable_sort(result, ranks_before);
    if (max_results != 0 && result.size() > max_results) result.resize(max_results);

    return result;
}

std::vector<Candidate> Dictionary::search_with_particle(const std::string& prefix) const {
    std::vector<Candidate> result;
    const std::string_view input = prefix;

    for (const auto particle : kParticles) {
        if (input.size() <= particle.size() || !input.ends_with(particle)) continue;

        const std::string stem(input.substr(0, input.size() - particle.size()));
        auto range = std::ranges::equal_range(entries_, stem, {}, &Candidate::reading);
        for (const auto& e : range) {
            Candidate c = e;
            c.reading    = prefix;
            c.text      += particle;
            c.id         = c.reading + ':' + c.text;
            c.base_score = e.base_score * kParticleScoreFactor;
            result.push_back(std::move(c));
        }
    }

    std::ranges::stable_sort(result, ranks_before);
    return result;
}

} // namespace propico::engine
