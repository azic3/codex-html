#include "config.h"
#include "webserver.h"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    std::string user = "root";
    std::string password = "Chencong123..";
    std::string databasename = "qgydb";

    Config config;
    config.parse_arg(argc, argv);

    WebServer server;
    server.init(config.PORT,
                user,
                password,
                databasename,
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
        std::cerr << "server start failed: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
