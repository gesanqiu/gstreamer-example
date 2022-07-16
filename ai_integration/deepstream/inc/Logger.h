/*
 * @Description: Single Instance logger based on spdlog.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-09-11 20:05:38
 * @Last Editor: Ricardo Lu
 * @LastEditTime: 2022-07-09 08:22:06
 */
#pragma once

#include <iostream>
#include <cstring>
#include <sstream>
#include <time.h>
#include <chrono>
#include <memory>

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

static inline int NowDateToInt()
{
    time_t now;
    time(&now);

    tm p;
    localtime_r(&now, &p);
    int now_date =(1900 + p.tm_year) * 10000 +(p.tm_mon + 1) * 100 + p.tm_mday;
    return now_date;
}

static inline int NowTimeToInt()
{
    time_t now;
    time(&now);

    tm p;
    localtime_r(&now, &p);

    int now_int = p.tm_hour * 10000 + p.tm_min * 100 + p.tm_sec;
    return now_int;
}

static inline spdlog::level::level_enum GetLogLevel(std::string& level)
{
    if (!(level.compare("trace"))) {
        return spdlog::level::trace;
    } else if (!(level.compare("debug"))) {
        return spdlog::level::debug;
    } else if (!(level.compare("info"))) {
        return spdlog::level::info;
    } else if (!(level.compare("warn"))) {
        return spdlog::level::warn;
    } else if (!(level.compare("error"))) {
        return spdlog::level::err;
    }
    return spdlog::level::trace;
}

class XLogger {
public:
    static XLogger* getInstance() {
        static XLogger xlogger;
        return &xlogger;
    }

    std::shared_ptr<spdlog::logger> getLogger() {
        return m_logger;
    }
private:
    XLogger() {
        try {
#ifdef DUMP_LOG
            int date = NowDateToInt();
            int timestamp = NowTimeToInt();
            std::stringstream file_logger_name;
            std::stringstream file_log_full_path;

            if (access(LOG_PATH, F_OK)) {
                spdlog::warn("Log diretory not exist, mkdir called");
                mkdir(LOG_PATH, S_IRWXU);
            }

            file_logger_name  << LOG_FILE_PREFIX << "_" << date << "_" << timestamp;
            file_log_full_path << LOG_PATH << "/" << file_logger_name.str() << ".log";

    #ifdef MULTI_LOG
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>
                                (file_log_full_path.str(), true);

            spdlog::logger logger("multi_sink", {console_sink, file_sink});
            m_logger = std::make_shared<spdlog::logger>(logger);
    #else // fileout only
            m_logger = spdlog::basic_logger_mt("file_logger", file_log_full_path.str());
    #endif // MULTI_LOG
#else // stdout only
            m_logger = spdlog::stdout_color_mt("console_logger");
#endif // DUMP_LOG

            m_logger->set_pattern("%Y-%m-%d %H:%M:%S.%f <thread %t> [%^%l%$] [%@] [%!] %v");

            std::string log_level(LOG_LEVEL);
            spdlog::info("Set log level to {}.", log_level);
            m_logger->set_level(GetLogLevel(log_level));
            m_logger->flush_on(GetLogLevel(log_level));
        } catch(const spdlog::spdlog_ex& ex) {
            spdlog::error("XLogger initializetion failed: {}", ex.what());
        }
    }

    ~XLogger() {
        spdlog::drop_all(); // must do this
    }

    XLogger(const XLogger&) = delete;
    XLogger& operator=(const XLogger&) = delete;
private:
    std::shared_ptr<spdlog::logger> m_logger;
};

// use embedded macro to support file and line number
#define LOG_TRACE(...) SPDLOG_LOGGER_CALL(XLogger::getInstance()->getLogger().get(), spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_CALL(XLogger::getInstance()->getLogger().get(), spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_LOGGER_CALL(XLogger::getInstance()->getLogger().get(), spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_LOGGER_CALL(XLogger::getInstance()->getLogger().get(), spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_CALL(XLogger::getInstance()->getLogger().get(), spdlog::level::err, __VA_ARGS__)
