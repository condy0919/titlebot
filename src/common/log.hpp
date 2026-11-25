#ifndef TITLEBOT_LOG_HPP_
#define TITLEBOT_LOG_HPP_

#include <spdlog/spdlog.h>

#include <cstdlib>

#define TB_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define TB_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define TB_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define TB_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define TB_LOG_FATAL(...)                                                                          \
    do {                                                                                           \
        SPDLOG_CRITICAL(__VA_ARGS__);                                                              \
        spdlog::default_logger()->flush();                                                         \
        std::exit(EXIT_FAILURE);                                                                   \
    } while (false)

#endif // TITLEBOT_LOG_HPP_
