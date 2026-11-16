#include <gtest/gtest.h>

#include "CliArguments.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace optinspect;

namespace {

CliArguments parse(std::vector<std::string> args) {
    args.insert(args.begin(), "optinspect");
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    return parseArguments(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST(CliArgumentsTest, Phase1InvocationStillWorksWithoutDatabaseOptions) {
    const CliArguments args = parse({"--reference", "ref.png", "--input", "in.png"});
    EXPECT_EQ(args.reference_path, "ref.png");
    EXPECT_EQ(args.inspected_path, "in.png");
    EXPECT_EQ(args.output_dir, "results");
    EXPECT_FALSE(args.database_path.has_value());
    EXPECT_FALSE(args.no_database);
    EXPECT_FALSE(args.product_id.has_value());
}

TEST(CliArgumentsTest, ExplicitInspectWordIsOptional) {
    const CliArguments args = parse({"inspect", "--reference", "ref.png", "--input", "in.png"});
    EXPECT_EQ(args.reference_path, "ref.png");
}

TEST(CliArgumentsTest, ParsesPersistenceOptions) {
    const CliArguments args =
        parse({"inspect", "--reference", "r", "--input", "i", "--database", "data/x.db", "--product-id", "7",
               "--serial", "SN-1", "--notes", "hello"});
    EXPECT_EQ(args.database_path, std::optional<std::string>("data/x.db"));
    EXPECT_EQ(args.product_id, std::optional<std::int64_t>(7));
    EXPECT_EQ(args.serial_number, std::optional<std::string>("SN-1"));
    EXPECT_EQ(args.notes, std::optional<std::string>("hello"));
}

TEST(CliArgumentsTest, RejectsInvalidDatabaseArguments) {
    EXPECT_THROW(parse({"--reference", "r", "--input", "i", "--product-id", "abc"}), std::invalid_argument);
    EXPECT_THROW(parse({"--reference", "r", "--input", "i", "--database"}), std::invalid_argument);
    EXPECT_THROW(parse({"--reference", "r", "--input", "i", "--database", "a.db", "--no-database"}),
                 std::invalid_argument);
    EXPECT_THROW(parse({"--reference", "r", "--input", "i", "--unknown"}), std::invalid_argument);
    EXPECT_THROW(parse({"--reference", "r"}), std::invalid_argument);
}

TEST(CliArgumentsTest, OptionMapParsingIsStrict) {
    const std::vector<std::string> args = {"--a", "1", "--b", "two"};
    const OptionMap options = parseOptionMap(args, 0, {"--a", "--b"});
    EXPECT_EQ(options.at("--a"), "1");
    EXPECT_EQ(options.at("--b"), "two");

    EXPECT_THROW(parseOptionMap({"--c", "x"}, 0, {"--a"}), std::invalid_argument);
    EXPECT_THROW(parseOptionMap({"--a"}, 0, {"--a"}), std::invalid_argument);
    EXPECT_THROW(parseOptionMap({"--a", "1", "--a", "2"}, 0, {"--a"}), std::invalid_argument);
}

TEST(CliArgumentsTest, NumericOptionConversionsRejectGarbage) {
    EXPECT_EQ(parseInt64Option("42", "--n"), 42);
    EXPECT_EQ(parseIntOption("-3", "--n"), -3);
    EXPECT_DOUBLE_EQ(parseDoubleOption("2.5", "--x"), 2.5);
    EXPECT_THROW(parseInt64Option("4x", "--n"), std::invalid_argument);
    EXPECT_THROW(parseInt64Option("", "--n"), std::invalid_argument);
    EXPECT_THROW(parseIntOption("99999999999", "--n"), std::invalid_argument);
    EXPECT_THROW(parseDoubleOption("abc", "--x"), std::invalid_argument);
}
