#pragma once
#include <string>
#include <cstdio>
#include <windows.h>

inline void LogOutput(const char* level, const char* msg) {
	char buffer[2048];
	std::snprintf(buffer, sizeof(buffer), "[%s] %s\n", level, msg);
	OutputDebugStringA(buffer);
	std::printf("%s", buffer);
}

inline void LOGI(const std::string& s) { LogOutput("INFO", s.c_str()); }
inline void LOGW(const std::string& s) { LogOutput("WARN", s.c_str()); }
inline void LOGE(const std::string& s) { LogOutput("ERROR", s.c_str()); }
