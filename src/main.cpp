#include "config.h"
#include "app_logger.h"
#include "webserver.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>

namespace
{
std::string getenv_or_default(const char *name, const std::string &fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    return value;
}

int getenv_int_or_default(const char *name, int fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }

    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > std::numeric_limits<int>::max())
    {
        AppLogger::error(std::string("Invalid integer environment value for ") + name + "; using default.");
        return fallback;
    }

    return static_cast<int>(parsed);
}

std::size_t getenv_size_or_default(const char *name, std::size_t fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }

    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0 ||
        parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
    {
        AppLogger::error(std::string("Invalid byte-size environment value for ") + name + "; using default.");
        return fallback;
    }

    return static_cast<std::size_t>(parsed);
}
}

int main(int argc, char *argv[])
{
    std::string user = getenv_or_default("DB_USER", "root");
    std::string password = getenv_or_default("DB_PASS", "");
    std::string databasename = getenv_or_default("DB_NAME", "qgydb");
    std::string databasehost = getenv_or_default("DB_HOST", "127.0.0.1");
    int databaseport = getenv_int_or_default("DB_PORT", 3306);
    std::string staticroot = getenv_or_default("XIAOCHEN_STATIC_ROOT", "");
    std::size_t max_image_upload_size = getenv_size_or_default("XIAOCHEN_MAX_IMAGE_UPLOAD_BYTES", 20ULL * 1024ULL * 1024ULL);
    std::size_t max_video_upload_size = getenv_size_or_default("XIAOCHEN_MAX_VIDEO_UPLOAD_BYTES", 1024ULL * 1024ULL * 1024ULL);

    if (password.empty())
    {
        AppLogger::error("DB_PASS is not set; refusing to start with a hard-coded database password.");
        std::cerr << "DB_PASS is not set. Load config/local/.env or export DB_PASS before starting." << std::endl;
        return 1;
    }

    Config config;
    config.parse_arg(argc, argv);

    WebServer server;
    server.init(config.PORT,
                user,
                password,
                databasename,
                databasehost,
                databaseport,
                staticroot,
                max_image_upload_size,
                max_video_upload_size,
                config.LOGWrite,
                config.OPT_LINGER,
                config.TRIGMode,
                config.sql_num,
                config.thread_num,
                config.close_log,
                config.actor_model);

    try
    {
        server.eventListen();
        server.eventLoop();
    }
    catch (const std::exception &ex)
    {
        AppLogger::error(std::string("server start failed: ") + ex.what());
        return 1;
    }

    return 0;
}
