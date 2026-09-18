// `hgps serve`'s command line, and the one rule that makes "no authentication" defensible.
#include "serve_options.h"

#include "server.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

hgps::app::ServeOptionsResult parse(const std::vector<std::string> &arguments) {
    return hgps::app::parse_serve_options(arguments);
}

} // namespace

TEST(ServeCommandLine, TheDefaultsAreLoopbackAnd8080) {
    const auto result = parse({});
    ASSERT_TRUE(result.options.has_value()) << result.message;
    EXPECT_EQ("127.0.0.1", result.options->host);
    EXPECT_EQ(8080, result.options->port);
    EXPECT_EQ("hgps-runs", result.options->runs_root.string());
}

TEST(ServeCommandLine, EveryOptionIsRead) {
    const auto result = parse({"--host", "localhost", "--port", "0", "--configs", "a",
                               "--configs", "b", "--runs", "r", "--web", "w", "--schema", "s"});
    ASSERT_TRUE(result.options.has_value()) << result.message;
    const auto &options = *result.options;
    EXPECT_EQ("localhost", options.host);
    EXPECT_EQ(0, options.port);
    EXPECT_EQ(std::vector<std::filesystem::path>({"a", "b"}), options.config_roots);
    EXPECT_EQ("r", options.runs_root.string());
    EXPECT_EQ("w", options.web_root.string());
    EXPECT_EQ("s", options.schema_path.string());
}

TEST(ServeCommandLine, APortThatIsNotOneIsRefused) {
    for (const auto *value : {"", "-1", "70000", "eighty", "80x"}) {
        const auto result = parse({"--port", value});
        EXPECT_FALSE(result.options.has_value()) << "'" << value << "' was accepted";
        EXPECT_NE(std::string::npos, result.message.find("--port")) << value;
    }
    const auto missing = parse({"--port"});
    EXPECT_FALSE(missing.options.has_value());
}

TEST(ServeCommandLine, AnUnknownOptionIsNamedRatherThanIgnored) {
    const auto result = parse({"--nope"});
    EXPECT_FALSE(result.options.has_value());
    EXPECT_NE(std::string::npos, result.message.find("--nope"));
}

TEST(ServeCommandLine, HelpNeedsNothingElse) {
    for (const auto *flag : {"-h", "--help"}) {
        const auto result = parse({flag});
        ASSERT_TRUE(result.options.has_value()) << flag;
        EXPECT_TRUE(result.options->help) << flag;
    }
    const auto text = hgps::app::serve_usage_text();
    EXPECT_NE(std::string::npos, text.find("--host"));
    EXPECT_NE(std::string::npos, text.find("--web"));
    EXPECT_NE(std::string::npos, text.find("docs/server-api.md"));
    // The help says *why* the host is restricted, because a message that only says "refused" sends
    // the reader looking for a flag to override it.
    EXPECT_NE(std::string::npos, text.find("authentication"));
}

TEST(ServeCommandLine, ParsingDoesNotBindAnything) {
    // A non-loopback host parses fine; it is refused when the server is asked to use it. Keeping
    // those apart is what lets `--help` work with any arguments and lets the refusal be tested
    // without opening a socket.
    const auto result = parse({"--host", "0.0.0.0"});
    ASSERT_TRUE(result.options.has_value()) << result.message;
    EXPECT_EQ("0.0.0.0", result.options->host);
    EXPECT_FALSE(hgps::server::loopback_refusal(result.options->host).empty());
}
