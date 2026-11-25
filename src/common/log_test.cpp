#include "log.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

namespace {
class LogTest : public testing::Test {
protected:
    void SetUp() override {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_st>(output);
        auto logger = std::make_shared<spdlog::logger>("log-test", sink);
        logger->set_pattern("%l %v");
        logger->set_level(spdlog::level::debug);
        spdlog::set_default_logger(logger);
    }

    void TearDown() override {
        spdlog::set_default_logger(previous);
    }

    std::ostringstream output;
    std::shared_ptr<spdlog::logger> previous = spdlog::default_logger();
};

TEST_F(LogTest, ForwardsLevelsAndFormatArguments) {
    TB_LOG_DEBUG("debug {}", 1);
    TB_LOG_INFO("info {}", 2);
    TB_LOG_WARN("warn {}", 3);
    TB_LOG_ERROR("error {}", 4);
    EXPECT_EQ(output.str(), "debug debug 1\ninfo info 2\nwarning warn 3\nerror error 4\n");
}

// GoogleTest sees the buffered message on stderr only when the logger flushes the stream.
class FlushToStderr : public std::stringbuf {
    int sync() override {
        std::fputs(str().c_str(), stderr);
        str("");
        return 0;
    }
};

TEST_F(LogTest, FatalFlushesCriticalMessageAndExitsWithFailure) {
    EXPECT_EXIT(
        {
            FlushToStderr buffer;
            std::ostream stream(&buffer);
            auto sink = std::make_shared<spdlog::sinks::ostream_sink_st>(stream);
            auto logger = std::make_shared<spdlog::logger>("fatal-test", sink);
            logger->set_pattern("%l %v");
            spdlog::set_default_logger(logger);
            TB_LOG_FATAL("fatal {}", 5);
        },
        testing::ExitedWithCode(EXIT_FAILURE), "critical fatal 5");
}
} // namespace
