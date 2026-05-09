#ifndef CONFIG_H
#define CONFIG_H

class Config
{
public:
    Config();
    ~Config() = default;

    void parse_arg(int argc, char *argv[]);

public:
    int PORT;
    int LOGWrite;
    int TRIGMode;
    int LISTENTrigmode;
    int CONNTrigmode;
    int OPT_LINGER;
    int sql_num;
    int thread_num;
    int close_log;
    int actor_model;
};

#endif
