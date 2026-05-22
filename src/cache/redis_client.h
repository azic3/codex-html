#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <string>
#include <vector>

class RedisClient
{
public:
    RedisClient();

    void init_from_env();
    bool enabled() const;
    std::string last_error() const;

    bool get(const std::string &key, std::string &value) const;
    bool setex(const std::string &key, int ttl_seconds, const std::string &value) const;
    bool del(const std::string &key) const;
    bool incr(const std::string &key, long long &value) const;
    bool expire(const std::string &key, int ttl_seconds) const;
    bool ttl(const std::string &key, long long &seconds) const;

private:
    struct RespValue
    {
        char type;
        std::string text;
        long long integer;
        bool nil;
    };

    bool command(const std::vector<std::string> &args, RespValue &reply) const;
    bool send_command(int fd, const std::vector<std::string> &args) const;
    bool read_reply(int fd, RespValue &reply) const;
    bool parse_reply(const std::string &buffer, std::size_t &offset, RespValue &reply) const;
    int connect_socket() const;
    void set_last_error(const std::string &error) const;

private:
    bool m_enabled;
    std::string m_host;
    int m_port;
    std::string m_password;
    int m_db;
    int m_timeout_ms;
    mutable std::string m_last_error;
};

#endif
