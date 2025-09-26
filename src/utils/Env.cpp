#include "Env.h"
#include "Logger.h"
#include <cstdlib>
#include <algorithm>
#include <cctype>

bool Env::GetBool(const char* name, bool defaultValue) {
    if (!name) return defaultValue;

    std::string value = GetString(name);
    if (value.empty()) {
        return defaultValue;
    }

    return ParseBool(value);
}

std::string Env::GetString(const char* name) {
    if (!name) return "";

#ifdef _MSC_VER
    // Use _dupenv_s to avoid C4996 warning on MSVC
    char* buffer = nullptr;
    size_t size = 0;
    errno_t err = _dupenv_s(&buffer, &size, name);

    if (err != 0 || buffer == nullptr) {
        return "";
    }

    std::string result(buffer);
    free(buffer);  // Must free the buffer returned by _dupenv_s
    return result;
#else
    // Use standard getenv on non-MSVC platforms
    const char* value = std::getenv(name);
    return value ? std::string(value) : "";
#endif
}

int Env::GetInt(const char* name, int defaultValue) {
    if (!name) return defaultValue;

    std::string value = GetString(name);
    if (value.empty()) {
        return defaultValue;
    }

    try {
        return std::stoi(value);
    } catch (...) {
        char errorMsg[256];
        std::snprintf(errorMsg, sizeof(errorMsg),
                      "Env::GetInt - Invalid integer value '%s' for %s, using default %d",
                      value.c_str(), name, defaultValue);
        LOGW(errorMsg);
        return defaultValue;
    }
}

bool Env::ParseBool(const std::string& value) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Check for various true values
    if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        return true;
    }

    return false;
}