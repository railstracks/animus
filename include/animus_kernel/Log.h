#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <atomic>
#include <ctime>
#include <iomanip>

namespace animus::kernel {

// ============================================================================
// Structured Logging Facade
// ============================================================================
// Minimal header-only logger with levels and categories.
// No external dependencies. Thread-safe via atomic level threshold.
//
// Usage:
//   ALOG_INFO("chain", "Executing tool " << toolName);
//   ALOG_ERROR("provider", "HTTP " << status << ": " << body);
//   ALOG_DEBUG("channel", "Polling " << url);   // compiled out unless ANIMUS_ALOG_DEBUG
//
// Level threshold is set globally. Default: Info (Debug/Trace compiled out
// in release builds via ANIMUS_ALOG_DEBUG preprocessor flag).
//
// Categories are free-form strings — no enum needed. Common categories:
//   chain, provider, channel, tool, store, context, memory, scheduler,
//   admin, auth, session, consolidation, node, irc, discord, whatsapp,
//   telegram, vk, slack, email, nextcloud, llm, diffusion
// ============================================================================

enum class LogLevel : int {
    Trace   = 0,
    Debug   = 1,
    Info    = 2,
    Warning = 3,
    Error   = 4,
    None    = 5  // silence everything
};

namespace detail {

// Global threshold — atomic for thread safety
inline std::atomic<int> g_logLevel{static_cast<int>(LogLevel::Info)};

inline LogLevel GetLogLevel() {
    return static_cast<LogLevel>(g_logLevel.load(std::memory_order_relaxed));
}

inline void SetLogLevel(LogLevel level) {
    g_logLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

inline void LogImpl(LogLevel level, std::string_view category, const std::string& message) {
    const char* levelStr = "?";
    switch (level) {
        case LogLevel::Trace:   levelStr = "TRACE"; break;
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO";  break;
        case LogLevel::Warning: levelStr = "WARN";  break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
        default: break;
    }

    // Timestamp (UTC, millisecond precision)
    auto now = std::time(nullptr);
    auto* tm = std::gmtime(&now);
    char ts[24];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm);

    std::cerr << "[" << ts << "] [" << levelStr << "] ["
              << category << "] " << message << std::endl;
}

} // namespace detail

// --- Public API ---

inline void SetLogLevel(LogLevel level) { detail::SetLogLevel(level); }
inline LogLevel GetLogLevel() { return detail::GetLogLevel(); }
inline void SetLogLevelFromString(const std::string& s) {
    if (s == "trace" || s == "0") SetLogLevel(LogLevel::Trace);
    else if (s == "debug" || s == "1") SetLogLevel(LogLevel::Debug);
    else if (s == "info" || s == "2") SetLogLevel(LogLevel::Info);
    else if (s == "warn" || s == "warning" || s == "3") SetLogLevel(LogLevel::Warning);
    else if (s == "error" || s == "4") SetLogLevel(LogLevel::Error);
    else if (s == "none" || s == "5") SetLogLevel(LogLevel::None);
}

// --- Logging macros ---

// Always-on logs (Warning + Error)
#define ALOG_WARNING(cat, msg) \
    do { \
        if (detail::GetLogLevel() <= LogLevel::Warning) \
            ::animus::kernel::detail::LogImpl(LogLevel::Warning, cat, \
                (static_cast<std::ostringstream&&>(std::ostringstream() << msg)).str()); \
    } while(0)

#define ALOG_ERROR(cat, msg) \
    do { \
        if (detail::GetLogLevel() <= LogLevel::Error) \
            ::animus::kernel::detail::LogImpl(LogLevel::Error, cat, \
                (static_cast<std::ostringstream&&>(std::ostringstream() << msg)).str()); \
    } while(0)

// Info-level — always evaluated, may be filtered at runtime
#define ALOG_INFO(cat, msg) \
    do { \
        if (detail::GetLogLevel() <= LogLevel::Info) \
            ::animus::kernel::detail::LogImpl(LogLevel::Info, cat, \
                (static_cast<std::ostringstream&&>(std::ostringstream() << msg)).str()); \
    } while(0)

// Debug-level — compiled out unless ANIMUS_ALOG_DEBUG is defined
#ifdef ANIMUS_ALOG_DEBUG
#define ALOG_DEBUG(cat, msg) \
    do { \
        if (detail::GetLogLevel() <= LogLevel::Debug) \
            ::animus::kernel::detail::LogImpl(LogLevel::Debug, cat, \
                (static_cast<std::ostringstream&&>(std::ostringstream() << msg)).str()); \
    } while(0)

#define ALOG_TRACE(cat, msg) \
    do { \
        if (detail::GetLogLevel() <= LogLevel::Trace) \
            ::animus::kernel::detail::LogImpl(LogLevel::Trace, cat, \
                (static_cast<std::ostringstream&&>(std::ostringstream() << msg)).str()); \
    } while(0)
#else
#define ALOG_DEBUG(cat, msg) do {} while(0)
#define ALOG_TRACE(cat, msg) do {} while(0)
#endif

} // namespace animus::kernel
