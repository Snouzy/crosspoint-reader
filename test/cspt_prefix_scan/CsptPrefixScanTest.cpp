#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "util/Dictionary.h"

namespace {

// Mirrors the .idx/.cspt layout produced by scripts/dictionary_tools.py:
// .idx entries are word\0 + 8-byte BE (dict offset, dict size); the .cspt
// samples ordinals 0, 16, 32, ... storing word[:16] null-padded + LE offset.
void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

void appendLe32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 24));
}

int ciCompare(const std::string& a, const std::string& b) {
  size_t i = 0;
  while (i < a.size() && i < b.size()) {
    const int diff = std::tolower(static_cast<unsigned char>(a[i])) - std::tolower(static_cast<unsigned char>(b[i]));
    if (diff != 0) return diff;
    ++i;
  }
  return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

class CsptPrefixScanTest : public testing::Test {
 protected:
  static constexpr const char* kFolder = "/dict/test";

  void SetUp() override {
    // 52 entries: 10 fillers, 40 headwords sharing the 16-byte prefix
    // "revolution franx" (one capitalized, case-insensitively grouped with its
    // twin), and a trailing word. The franx bucket spans samples 16/32/48, so
    // the target below can sort before the last in-bucket sample — the exact
    // layout that used to hide entries from the forward-only scan.
    words = {
        "filler00", "filler01", "filler02", "filler03", "filler04", "filler05", "filler06", "filler07", "filler08",
        "filler09", "revolution franx00", "revolution franx01", "revolution franx02", "revolution franx03",
        "revolution franx04", "revolution franx05", "revolution franx06", "revolution franx07", "revolution franx08",
        "revolution franx09", "Revolution franx10", "revolution franx10", "revolution franx11", "revolution franx12",
        "revolution franx13", "revolution franx14", "revolution franx15", "revolution franx16", "revolution franx17",
        "revolution franx18", "revolution franx19", "revolution franx20", "revolution franx21", "revolution franx22",
        "revolution franx23", "revolution franx24", "revolution franx25", "revolution franx26", "revolution franx27",
        "revolution franx28", "revolution franx29", "revolution franx30", "revolution franx31", "revolution franx32",
        "revolution franx33", "revolution franx34", "revolution franx35", "revolution franx36", "revolution franx37",
        "revolution franx38", "revolution franx39", "zebra",
    };
    ASSERT_TRUE(std::is_sorted(words.begin(), words.end(),
                               [](const std::string& a, const std::string& b) { return ciCompare(a, b) < 0; }));

    std::vector<uint8_t> idx;
    std::vector<uint32_t> offsets;
    for (size_t i = 0; i < words.size(); ++i) {
      offsets.push_back(static_cast<uint32_t>(idx.size()));
      idx.insert(idx.end(), words[i].begin(), words[i].end());
      idx.push_back(0);
      appendBe32(idx, static_cast<uint32_t>(i * 1000));  // dict offset
      appendBe32(idx, 7);                                // dict size
    }

    std::vector<uint8_t> cspt = {'C', 'S', 'P', 'T', 1, 16, 16, 0, 0, 0, 0, 0};
    std::vector<uint8_t> entries;
    uint32_t sampleCount = 0;
    for (size_t i = 0; i < words.size(); i += 16) {
      std::string prefix = words[i].substr(0, 16);
      prefix.resize(16, '\0');
      entries.insert(entries.end(), prefix.begin(), prefix.end());
      appendLe32(entries, offsets[i]);
      ++sampleCount;
    }
    cspt[8] = static_cast<uint8_t>(sampleCount);
    cspt[9] = static_cast<uint8_t>(sampleCount >> 8);
    cspt[10] = static_cast<uint8_t>(sampleCount >> 16);
    cspt[11] = static_cast<uint8_t>(sampleCount >> 24);
    cspt.insert(cspt.end(), entries.begin(), entries.end());

    Storage.reset();
    Storage.put("/.crosspoint/dictionary.bin", std::string(kFolder));
    Storage.put(std::string(kFolder) + ".idx", idx);
    Storage.put(std::string(kFolder) + ".idx.oft.cspt", cspt);
  }

  size_t ordinalOf(const std::string& word) const {
    const auto found = std::find(words.begin(), words.end(), word);
    EXPECT_NE(found, words.end());
    return static_cast<size_t>(std::distance(words.begin(), found));
  }

  DictLocation locate(const std::string& word) { return Dictionary::locate(word); }

  std::vector<std::string> words;
};

TEST_F(CsptPrefixScanTest, FindsWordSortingBeforeLastInBucketSample) {
  // Ordinal 18; the last franx-bucket sample (ordinal 48) sorts after it, so
  // anchoring on the last in-bucket sample starts the scan past this word.
  const DictLocation result = locate("revolution franx07");
  ASSERT_TRUE(result.found) << "word hidden by its 16-byte prefix bucket";
  EXPECT_FALSE(result.readError);
  EXPECT_EQ(result.headword, "revolution franx07");
  EXPECT_EQ(result.offset, ordinalOf("revolution franx07") * 1000);
  EXPECT_EQ(result.size, 7u);
}

TEST_F(CsptPrefixScanTest, FindsFirstWordInBucket) {
  const DictLocation result = locate("revolution franx00");
  ASSERT_TRUE(result.found);
  EXPECT_EQ(result.headword, "revolution franx00");
}

TEST_F(CsptPrefixScanTest, FindsLastWordInBucket) {
  const DictLocation result = locate("revolution franx39");
  ASSERT_TRUE(result.found);
  EXPECT_EQ(result.headword, "revolution franx39");
}

TEST_F(CsptPrefixScanTest, FindsWordThatSitsExactlyOnASample) {
  const DictLocation result = locate("revolution franx21");  // ordinal 32, a sample
  ASSERT_TRUE(result.found);
  EXPECT_EQ(result.headword, "revolution franx21");
}

TEST_F(CsptPrefixScanTest, CaseVariantInsideBucketIsFound) {
  const DictLocation capital = locate("Revolution franx10");
  ASSERT_TRUE(capital.found);
  EXPECT_EQ(capital.headword, "Revolution franx10");

  const DictLocation lower = locate("revolution franx10");
  ASSERT_TRUE(lower.found);
  EXPECT_EQ(lower.headword, "revolution franx10");
}

TEST_F(CsptPrefixScanTest, FindsWordsOutsideAnyBucket) {
  const DictLocation filler = locate("filler05");
  ASSERT_TRUE(filler.found);
  EXPECT_EQ(filler.headword, "filler05");

  const DictLocation trailing = locate("zebra");
  ASSERT_TRUE(trailing.found);
  EXPECT_EQ(trailing.headword, "zebra");
}

TEST_F(CsptPrefixScanTest, MissesAreStillMisses) {
  const DictLocation beforeAll = locate("aaa");
  EXPECT_FALSE(beforeAll.found);
  EXPECT_FALSE(beforeAll.readError);

  const DictLocation afterAll = locate("zzz");
  EXPECT_FALSE(afterAll.found);
  EXPECT_FALSE(afterAll.readError);

  const DictLocation insideBucket = locate("revolution franx55");
  EXPECT_FALSE(insideBucket.found);
  EXPECT_FALSE(insideBucket.readError);
}

}  // namespace
