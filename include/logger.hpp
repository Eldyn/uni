#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <chrono>
#include <ctime>

struct Logger {
private:
    static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()) % 1000;

        time_t t = std::chrono::system_clock::to_time_t(now);
        tm tm_buf;
        #ifdef _WIN32
            localtime_s(&tm_buf, &t);   // In windows gli argomenti sono swappati
        #else
            localtime_r(&t, &tm_buf);   // POSIX
        #endif

        char buf[20];
        strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
        return std::string(buf) + "." + std::to_string(ms.count());
    }

public:
    static void Info(std::string_view msg) {
        std::cout << "[" << timestamp() << "] [INFO]  " << msg << "\n";
    }
    static void Warn(std::string_view msg) {
        std::cout << "[" << timestamp() << "] [WARN]  " << msg << "\n";
    }
    static void Error(std::string_view msg) {
        std::cout << "[" << timestamp() << "] [ERROR] " << msg << "\n";
    }

    template<typename... Args>
    static void Log(Args&&... args) {
        std::cout << "[" << timestamp() << "] ";
        (std::cout << ... << args);
        std::cout << "\n";
    }
};
