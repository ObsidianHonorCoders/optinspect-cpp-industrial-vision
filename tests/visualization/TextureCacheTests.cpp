#include "visualization/TextureCache.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace optinspect::viz;

namespace {

/// Stand-in for a GPU texture: counts live instances so ownership rules can be checked.
struct FakeTexture {
    static inline int live = 0;
    FakeTexture() { ++live; }
    ~FakeTexture() { --live; }
};

using Cache = BoundedLruCache<FakeTexture>;

std::shared_ptr<FakeTexture> make() { return std::make_shared<FakeTexture>(); }

}  // namespace

class TextureCacheTest : public ::testing::Test {
protected:
    void SetUp() override { FakeTexture::live = 0; }
    void TearDown() override { EXPECT_EQ(FakeTexture::live, 0) << "textures leaked"; }
};

TEST_F(TextureCacheTest, MissThenHit) {
    Cache cache(1000);
    EXPECT_EQ(cache.get("a"), nullptr);
    EXPECT_EQ(cache.stats().misses, 1u);

    auto t = make();
    cache.put("a", t, 100);
    EXPECT_EQ(cache.get("a"), t);
    EXPECT_EQ(cache.stats().hits, 1u);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.bytes(), 100u);
}

TEST_F(TextureCacheTest, ContainsDoesNotAffectStatsOrRecency) {
    Cache cache(1000);
    cache.put("a", make(), 10);
    cache.put("b", make(), 10);
    EXPECT_TRUE(cache.contains("a"));
    EXPECT_FALSE(cache.contains("zzz"));
    EXPECT_EQ(cache.stats().hits, 0u);
    EXPECT_EQ(cache.stats().misses, 0u);
    EXPECT_EQ(cache.keys().front(), "b");  // "a" was not promoted by contains()
}

TEST_F(TextureCacheTest, ReplacementKeepsOneEntryAndUpdatesBytes) {
    Cache cache(1000);
    auto first = make();
    cache.put("a", first, 100);
    auto second = make();
    cache.put("a", second, 250);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.bytes(), 250u);
    EXPECT_EQ(cache.get("a"), second);
    EXPECT_EQ(cache.stats().replacements, 1u);
    first.reset();  // only the cache's reference was dropped on replacement
    EXPECT_EQ(FakeTexture::live, 1);
}

TEST_F(TextureCacheTest, EvictsLeastRecentlyUsedWhenOverBudget) {
    Cache cache(300);
    cache.put("a", make(), 100);
    cache.put("b", make(), 100);
    cache.put("c", make(), 100);
    ASSERT_EQ(cache.size(), 3u);
    cache.get("a");  // a becomes most recently used; b is now the oldest
    cache.put("d", make(), 100);

    EXPECT_FALSE(cache.contains("b"));
    EXPECT_TRUE(cache.contains("a"));
    EXPECT_TRUE(cache.contains("c"));
    EXPECT_TRUE(cache.contains("d"));
    EXPECT_EQ(cache.bytes(), 300u);
    EXPECT_EQ(cache.stats().evictions, 1u);
}

TEST_F(TextureCacheTest, EvictionDestroysTexturesNobodyElseOwns) {
    Cache cache(100);
    cache.put("a", make(), 100);
    cache.put("b", make(), 100);  // evicts a
    EXPECT_EQ(FakeTexture::live, 1);
}

TEST_F(TextureCacheTest, EvictedTextureInUseStaysAliveUntilReleased) {
    Cache cache(100);
    auto in_use = make();
    cache.put("a", in_use, 100);
    cache.put("b", make(), 100);  // evicts "a" from the cache
    EXPECT_FALSE(cache.contains("a"));
    EXPECT_EQ(FakeTexture::live, 2);  // the drawing code's reference keeps it alive
    in_use.reset();
    EXPECT_EQ(FakeTexture::live, 1);
}

TEST_F(TextureCacheTest, NewestEntryIsNeverEvictedEvenIfLargerThanBudget) {
    Cache cache(50);
    cache.put("huge", make(), 500);
    EXPECT_TRUE(cache.contains("huge"));
    EXPECT_EQ(cache.size(), 1u);
    cache.put("next", make(), 10);
    EXPECT_TRUE(cache.contains("next"));
    EXPECT_FALSE(cache.contains("huge"));
}

TEST_F(TextureCacheTest, InvalidateRemovesOneEntry) {
    Cache cache(1000);
    cache.put("a", make(), 100);
    cache.put("b", make(), 100);
    EXPECT_TRUE(cache.invalidate("a"));
    EXPECT_FALSE(cache.invalidate("a"));
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.bytes(), 100u);
    EXPECT_EQ(cache.stats().invalidations, 1u);
    EXPECT_EQ(FakeTexture::live, 1);
}

TEST_F(TextureCacheTest, ClearReleasesEverything) {
    Cache cache(1000);
    cache.put("a", make(), 100);
    cache.put("b", make(), 100);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.bytes(), 0u);
    EXPECT_EQ(FakeTexture::live, 0);
}

TEST_F(TextureCacheTest, ShrinkingTheBudgetEvicts) {
    Cache cache(1000);
    for (int i = 0; i < 5; ++i) cache.put("k" + std::to_string(i), make(), 100);
    cache.setBudget(250);
    EXPECT_LE(cache.bytes(), 250u);
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_TRUE(cache.contains("k4"));  // most recent survive
    EXPECT_TRUE(cache.contains("k3"));
}

TEST_F(TextureCacheTest, KeysAreOrderedMostRecentFirst) {
    Cache cache(1000);
    cache.put("a", make(), 1);
    cache.put("b", make(), 1);
    cache.put("c", make(), 1);
    cache.get("a");
    const std::vector<std::string> keys = cache.keys();
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "a");
    EXPECT_EQ(keys[1], "c");
    EXPECT_EQ(keys[2], "b");
}

TEST_F(TextureCacheTest, ChangedFileKeyIsAMissAndDoesNotReuseTheOldEntry) {
    // The GUI keys textures by "path|mtime|size"; a rewritten file yields a new key.
    Cache cache(1000);
    cache.put("img.png|100|2000", make(), 100);
    EXPECT_EQ(cache.get("img.png|101|2000"), nullptr);
}
