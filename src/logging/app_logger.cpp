#include "app_logger.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>

namespace
{
std::mutex g_log_mutex;

std::string now_string()
{
    std::time_t now = std::time(nullptr);
    struct tm time_info;
#if defined(_WIN32)
    localtime_s(&time_info, &now);
#else
    localtime_r(&now, &time_info);
#endif

    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &time_info);
    return buffer;
}

void ensure_log_directory()
{
#if defined(_WIN32)
    mkdir("logs");
#else
    mkdir("logs", 0755);
#endif
}

std::string lower_copy(const std::string &value)
{
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool is_secret_key(const std::string &key)
{
    const std::string lowered = lower_copy(key);
    return lowered.find("password") != std::string::npos ||
           lowered.find("passwd") != std::string::npos ||
           lowered.find("email_code") != std::string::npos ||
           lowered.find("code") != std::string::npos ||
           lowered.find("token") != std::string::npos ||
           lowered.find("secret") != std::string::npos ||
           lowered.find("authorization") != std::string::npos ||
           lowered.find("smtp_password") != std::string::npos;
}

std::string redact_key_value_pairs(const std::string &value)
{
    std::string redacted = value;
    std::size_t pos = 0;
    while (pos < redacted.size())
    {
        std::size_t key_start = pos;
        while (key_start < redacted.size() &&
               !std::isalnum(static_cast<unsigned char>(redacted[key_start])) &&
               redacted[key_start] != '_' &&
               redacted[key_start] != '-')
        {
            ++key_start;
        }

        std::size_t key_end = key_start;
        while (key_end < redacted.size() &&
               (std::isalnum(static_cast<unsigned char>(redacted[key_end])) ||
                redacted[key_end] == '_' ||
                redacted[key_end] == '-'))
        {
            ++key_end;
        }

        if (key_end == key_start)
        {
            break;
        }

        std::size_t sep = key_end;
        while (sep < redacted.size() && (redacted[sep] == ' ' || redacted[sep] == '\t'))
        {
            ++sep;
        }

        if (sep >= redacted.size() || (redacted[sep] != '=' && redacted[sep] != ':'))
        {
            pos = key_end + 1;
            continue;
        }

        if (!is_secret_key(redacted.substr(key_start, key_end - key_start)))
        {
            pos = sep + 1;
            continue;
        }

        std::size_t value_start = sep + 1;
        while (value_start < redacted.size() &&
               (redacted[value_start] == ' ' || redacted[value_start] == '\t' || redacted[value_start] == '"' || redacted[value_start] == '\''))
        {
            ++value_start;
        }

        std::size_t value_end = value_start;
        while (value_end < redacted.size() &&
               redacted[value_end] != '&' &&
               redacted[value_end] != ',' &&
               redacted[value_end] != ';' &&
               redacted[value_end] != ' ' &&
               redacted[value_end] != '\t' &&
               redacted[value_end] != '\r' &&
               redacted[value_end] != '\n' &&
               redacted[value_end] != '"' &&
               redacted[value_end] != '\'')
        {
            ++value_end;
        }

        if (value_end > value_start)
        {
            redacted.replace(value_start, value_end - value_start, "[REDACTED]");
            pos = value_start + 10;
        }
        else
        {
            pos = value_end + 1;
        }
    }
    return redacted;
}
}

void AppLogger::info(const std::string &message)
{
    write("INFO", message, false);
}

void AppLogger::error(const std::string &message)
{
    write("ERROR", message, true);
}

std::string AppLogger::redact(const std::string &value)
{
    std::string redacted = redact_key_value_pairs(value);

    const char *sensitive_words[] = {
        "verification code",
        "email code",
        "smtp auth code",
        "smtp authorization code"};

    std::string lowered = lower_copy(redacted);
    for (std::size_t i = 0; i < sizeof(sensitive_words) / sizeof(sensitive_words[0]); ++i)
    {
        std::size_t pos = lowered.find(sensitive_words[i]);
        if (pos != std::string::npos)
        {
            const std::size_t colon = redacted.find(':', pos);
            if (colon != std::string::npos)
            {
                std::size_t end = redacted.find_first_of("\r\n,;", colon + 1);
                if (end == std::string::npos)
                {
                    end = redacted.size();
                }
                redacted.replace(colon + 1, end - colon - 1, " [REDACTED]");
                lowered = lower_copy(redacted);
            }
        }
    }

    return redacted;
}

std::string AppLogger::mask_email(const std::string &email)
{
    const std::size_t at = email.find('@');
    if (at == std::string::npos)
    {
        return "[invalid-email]";
    }

    std::string local = email.substr(0, at);
    const std::string domain = email.substr(at);
    if (local.size() <= 2)
    {
        return local.substr(0, 1) + "***" + domain;
    }
    return local.substr(0, 2) + "***" + domain;
}

void AppLogger::write(const std::string &level, const std::string &message, bool error_log)
{
    const std::string safe_message = redact(message);
    const std::string line = now_string() + " [" + level + "] " + safe_message;

    std::unique_lock<std::mutex> lock(g_log_mutex);
    ensure_log_directory();

    std::ofstream app("logs/app.log", std::ios::out | std::ios::app);
    if (app.is_open())
    {
        app << line << '\n';
    }

    if (error_log)
    {
        std::ofstream errors("logs/error.log", std::ios::out | std::ios::app);
        if (errors.is_open())
        {
            errors << line << '\n';
        }
        std::cerr << line << std::endl;
    }
    else
    {
        std::cout << line << std::endl;
    }
}
