#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <string>

class AppLogger
{
public:
    static void info(const std::string &message);
    static void error(const std::string &message);
    static std::string redact(const std::string &value);
    static std::string mask_email(const std::string &email);

private:
    static void write(const std::string &level, const std::string &message, bool error_log);
};

#endif
