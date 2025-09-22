#pragma once
#include <string>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <windows.h>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void SetLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file.is_open()) {
            m_file.close();
        }
        if (!filename.empty()) {
            m_file.open(filename, std::ios::out | std::ios::app);
            if (m_file.is_open()) {
                // Write header with timestamp
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                m_file << "\n=== PlasmaDX Log Session Started ===" << std::endl;
                m_file << "Time: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
                m_file << "=====================================\n" << std::endl;
                m_file.flush();
            }
        }
    }

    void Log(const char* level, const char* msg) {
        char buffer[2048];
        std::snprintf(buffer, sizeof(buffer), "[%s] %s\n", level, msg);

        // Output to debug console
        OutputDebugStringA(buffer);

        // Output to console
        std::printf("%s", buffer);

        // Output to file if configured
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_file.is_open()) {
                m_file << buffer;
                m_file.flush();  // Ensure immediate write
            }
        }
    }

    ~Logger() {
        if (m_file.is_open()) {
            m_file << "\n=== PlasmaDX Log Session Ended ===" << std::endl;
            m_file.close();
        }
    }

private:
    Logger() = default;
    std::ofstream m_file;
    std::mutex m_mutex;
};

inline void LogOutput(const char* level, const char* msg) {
    Logger::Instance().Log(level, msg);
}

inline void SetLogFile(const std::string& filename) {
    Logger::Instance().SetLogFile(filename);
}

inline void LOGI(const std::string& s) { LogOutput("INFO", s.c_str()); }
inline void LOGW(const std::string& s) { LogOutput("WARN", s.c_str()); }
inline void LOGE(const std::string& s) { LogOutput("ERROR", s.c_str()); }
