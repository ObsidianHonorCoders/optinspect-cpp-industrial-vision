#include "BatchDiscovery.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using optinspect::discoverPairs;

namespace {

class BatchDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "optinspect_batch_discovery_test";
        fs::remove_all(root_);
        fs::create_directories(root_ / "refs");
        fs::create_directories(root_ / "insp");
    }
    void TearDown() override { fs::remove_all(root_); }

    static void touch(const fs::path& p) { std::ofstream(p.string()).put('x'); }

    fs::path root_;
};

}  // namespace

TEST_F(BatchDiscoveryTest, PairsSharedDirectoryByIdSortedAndIgnoresOtherFiles) {
    touch(root_ / "refs" / "b002_temp.jpg");
    touch(root_ / "refs" / "b002_test.jpg");
    touch(root_ / "refs" / "a001_temp.jpg");
    touch(root_ / "refs" / "a001_test.jpg");
    touch(root_ / "refs" / "notes.txt");

    const auto d = discoverPairs(root_ / "refs", root_ / "refs");
    ASSERT_EQ(d.pairs.size(), 2u);
    EXPECT_EQ(d.pairs[0].id, "a001");
    EXPECT_EQ(d.pairs[1].id, "b002");
    EXPECT_TRUE(d.unmatched_references.empty());
    EXPECT_TRUE(d.unmatched_inspected.empty());
}

TEST_F(BatchDiscoveryTest, PairsAcrossDirectoriesWithDifferentExtensions) {
    touch(root_ / "refs" / "x1_temp.PNG");
    touch(root_ / "insp" / "x1_test.jpg");

    const auto d = discoverPairs(root_ / "refs", root_ / "insp");
    ASSERT_EQ(d.pairs.size(), 1u);
    EXPECT_EQ(d.pairs[0].reference_path.filename().string(), "x1_temp.PNG");
    EXPECT_EQ(d.pairs[0].inspected_path.filename().string(), "x1_test.jpg");
}

TEST_F(BatchDiscoveryTest, ReportsUnmatchedFilesOnBothSides) {
    touch(root_ / "refs" / "only_ref_temp.jpg");
    touch(root_ / "insp" / "only_insp_test.jpg");

    const auto d = discoverPairs(root_ / "refs", root_ / "insp");
    EXPECT_TRUE(d.pairs.empty());
    ASSERT_EQ(d.unmatched_references.size(), 1u);
    ASSERT_EQ(d.unmatched_inspected.size(), 1u);
}

TEST_F(BatchDiscoveryTest, ThrowsWhenPathIsNotADirectory) {
    touch(root_ / "file.jpg");
    EXPECT_THROW(discoverPairs(root_ / "file.jpg", root_ / "insp"), std::invalid_argument);
    EXPECT_THROW(discoverPairs(root_ / "refs", root_ / "missing"), std::invalid_argument);
}
