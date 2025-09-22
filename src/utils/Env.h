#pragma once
#include <string>

class Env {
public:
    // Get boolean environment variable (returns default if not set or invalid)
    // Recognizes: "1", "true", "TRUE", "on", "ON" as true
    static bool GetBool(const char* name, bool defaultValue = false);

    // Get string environment variable (returns empty string if not set)
    // Properly handles _dupenv_s to avoid MSVC warnings
    static std::string GetString(const char* name);

    // Get integer environment variable (returns default if not set or invalid)
    static int GetInt(const char* name, int defaultValue = 0);

private:
    // Helper to normalize boolean strings
    static bool ParseBool(const std::string& value);
};