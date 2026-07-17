#pragma once
#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @file logger.hpp
 * @brief Thread-safe, colour-formatted logging system for the console and an
 * optional plain-text file sink.
 */

/**
 * @enum LogLevel
 * @brief Severity ordering used to filter which calls actually print.
 *
 * `Log`, `WS`, `Lobby` and `HTTP` are per-message trace helpers and log at
 * `Debug` — noisy by default, so they're suppressed unless `LOG_LEVEL=debug`
 * is set. `Info`/`Warn`/`Error` map to their own levels.
 * @tag LOG-ENM-001
 */
enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, Silent = 4 };

/**
 * @struct Logger
 * @brief Provides static methods to print formatted log messages to standard output.
 * * Implements cross-platform ANSI colour support (enabling VT processing on Windows)
 * and automatically formats the messages with millisecond timestamps and aligned tags.
 * Configuration (minimum level, optional file sink) is read once from the
 * process environment (`LOG_LEVEL`, `LOG_FILE`) on first use.
 * @tag LOG-CLS-001
 */
struct Logger {
private:
    // ANSI Color Codes
    static constexpr std::string_view Reset   = "\033[0m";
    static constexpr std::string_view Gray    = "\033[90m";
    static constexpr std::string_view Red     = "\033[31m";
    static constexpr std::string_view Green   = "\033[32m";
    static constexpr std::string_view Yellow  = "\033[33m";
    static constexpr std::string_view Blue    = "\033[34m";
    static constexpr std::string_view Magenta = "\033[35m";
    static constexpr std::string_view Cyan    = "\033[36m";
    static constexpr std::string_view White   = "\033[37m";
    static constexpr std::string_view Bold    = "\033[1m";

    inline static bool       is_initialized = false;
    inline static LogLevel   threshold      = LogLevel::Info;
    inline static std::ofstream file_sink;
    inline static std::mutex mutex_;

    /**
     * @brief Parses `LOG_LEVEL` values ("debug"/"info"/"warn"/"error"/"silent",
     * case-insensitive) into a LogLevel, falling back to Info on anything else.
     * @tag LOG-PRIV-004
     */
    static LogLevel ParseLevel(std::string_view raw) {
        std::string s(raw);
        std::transform(s.begin(), s.end(), s.begin(),
                        [](unsigned char c) { return std::tolower(c); });
        if (s == "debug")  return LogLevel::Debug;
        if (s == "warn")   return LogLevel::Warn;
        if (s == "error")  return LogLevel::Error;
        if (s == "silent") return LogLevel::Silent;
        return LogLevel::Info;
    }

    /**
     * @brief One-time setup: enables ANSI on Windows, reads `LOG_LEVEL` to set
     * the print threshold, and opens `LOG_FILE` as a plain-text sink if set.
     * @tag LOG-PRIV-001
     */
    static void Init() {
        if (is_initialized) return;
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
        }
#endif
        if (const char* level = std::getenv("LOG_LEVEL")) {
            threshold = ParseLevel(level);
        }
        if (const char* path = std::getenv("LOG_FILE"); path && *path) {
            file_sink.open(path, std::ios::app);
        }
        is_initialized = true;
    }

    /**
     * @brief Generates a timestamp formatted with milliseconds (e.g. "[14:32:01.045]").
     * @param colored Wraps the timestamp in the grey ANSI code when true (console output).
     * @tag LOG-PRIV-002
     */
    static std::string timestamp(bool colored) {
        auto now = std::chrono::system_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()) % 1000;

        time_t t = std::chrono::system_clock::to_time_t(now);
        tm tm_buf;
        #ifdef _WIN32
            localtime_s(&tm_buf, &t);   // On Windows the arguments are swapped
        #else
            localtime_r(&t, &tm_buf);   // POSIX
        #endif

        char buf[20];
        strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);

        std::ostringstream oss;
        if (colored) oss << Gray;
        oss << "[" << buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";
        if (colored) oss << Reset;
        return oss.str();
    }

    /**
     * @brief Core function for formatting and printing. Drops the call entirely
     * if `level` is below the configured `LOG_LEVEL` threshold. Writes a
     * colourised line to stdout and, when `LOG_FILE` is set, an
     * ANSI-free mirror line to the file sink — both share the same
     * `[HH:MM:SS.mmm] [TAG] message` shape so either is trivially greppable.
     * Uses C++17 fold expressions to efficiently unpack the variadic arguments.
     * @tag LOG-PRIV-003
     */
    template<typename... Args>
    static void Print(LogLevel level, std::string_view color, std::string_view tag,
                       Args&&... args) {
        if (!is_initialized) Init();
        if (level < threshold) return;

        std::ostringstream body;
        (body << ... << std::forward<Args>(args));
        const std::string message = body.str();

        std::ostringstream console;
        console << timestamp(true) << " "
                << Gray << "[" << Reset << color << Bold << std::left << std::setw(5) << tag
                << Reset << Gray << "] " << Reset << message;

        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << console.str() << "\n";

        if (file_sink.is_open()) {
            file_sink << timestamp(false) << " [" << tag << "] " << message << "\n";
            file_sink.flush();
        }
    }

public:
    /**
     * @brief Logs a generic informational message (Green).
     * @tag LOG-MTH-001
     */
    template<typename... Args>
    static void Info(Args&&... args) {
        Print(LogLevel::Info, Green, "INFO", std::forward<Args>(args)...);
    }

    /**
     * @brief Logs a non-blocking warning (Yellow).
     * @tag LOG-MTH-002
     */
    template<typename... Args>
    static void Warn(Args&&... args) {
        Print(LogLevel::Warn, Yellow, "WARN", std::forward<Args>(args)...);
    }

    /**
     * @brief Logs a critical error (Red).
     * @tag LOG-MTH-003
     */
    template<typename... Args>
    static void Error(Args&&... args) {
        Print(LogLevel::Error, Red, "ERROR", std::forward<Args>(args)...);
    }

    /**
     * @brief Logs an event specific to the WebSocket layer (Cyan). Trace-level.
     * @tag LOG-MTH-004
     */
    template<typename... Args>
    static void WS(Args&&... args) {
        Print(LogLevel::Debug, Cyan, " WS ", std::forward<Args>(args)...);
    }

    /**
     * @brief Logs an event specific to the LobbyController (dark Yellow). Trace-level.
     * @tag LOG-MTH-005
     */
    template<typename... Args>
    static void Lobby(Args&&... args) {
        Print(LogLevel::Debug, Yellow, "LOBBY", std::forward<Args>(args)...);
    }

    /**
     * @brief Logs an event specific to HTTP requests (Magenta). Trace-level.
     * @tag LOG-MTH-006
     */
    template<typename... Args>
    static void HTTP(Args&&... args) {
        Print(LogLevel::Debug, Magenta, "HTTP", std::forward<Args>(args)...);
    }

    /**
     * @brief Logs a basic debug message (White). Trace-level.
     * @tag LOG-MTH-007
     */
    template<typename... Args>
    static void Log(Args&&... args) {
        Print(LogLevel::Debug, White, " LOG", std::forward<Args>(args)...);
    }

    static constexpr std::string_view ColorWhite() { return White; }
    static constexpr std::string_view ColorReset() { return Reset; }
};
