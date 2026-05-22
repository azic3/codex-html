#include "redis_client.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

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
    if (end == value || *end != '\0')
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool getenv_bool_or_default(const char *name, bool fallback)
{
    std::string value = getenv_or_default(name, fallback ? "1" : "0");
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool parse_line(const std::string &buffer, std::size_t offset, std::size_t &line_end)
{
    line_end = buffer.find("\r\n", offset);
    return line_end != std::string::npos;
}
}

RedisClient::RedisClient()
    : m_enabled(true),
      m_host("127.0.0.1"),
      m_port(6379),
      m_db(0),
      m_timeout_ms(1000)
{
}

void RedisClient::init_from_env()
{
    m_enabled = getenv_bool_or_default("XIAOCHEN_REDIS_ENABLED", true);
    m_host = getenv_or_default("XIAOCHEN_REDIS_HOST", "127.0.0.1");
    m_port = getenv_int_or_default("XIAOCHEN_REDIS_PORT", 6379);
    m_password = getenv_or_default("XIAOCHEN_REDIS_PASSWORD", "");
    m_db = getenv_int_or_default("XIAOCHEN_REDIS_DB", 0);
    m_timeout_ms = getenv_int_or_default("XIAOCHEN_REDIS_TIMEOUT_MS", 1000);
}

bool RedisClient::enabled() const
{
    return m_enabled;
}

std::string RedisClient::last_error() const
{
    return m_last_error;
}

bool RedisClient::get(const std::string &key, std::string &value) const
{
    RespValue reply;
    if (!command(std::vector<std::string>{"GET", key}, reply))
    {
        return false;
    }
    if (reply.nil)
    {
        value.clear();
        return false;
    }
    value = reply.text;
    return true;
}

bool RedisClient::setex(const std::string &key, int ttl_seconds, const std::string &value) const
{
    RespValue reply;
    return command(std::vector<std::string>{"SETEX", key, std::to_string(ttl_seconds), value}, reply);
}

bool RedisClient::del(const std::string &key) const
{
    RespValue reply;
    return command(std::vector<std::string>{"DEL", key}, reply);
}

bool RedisClient::incr(const std::string &key, long long &value) const
{
    RespValue reply;
    if (!command(std::vector<std::string>{"INCR", key}, reply) || reply.type != ':')
    {
        return false;
    }
    value = reply.integer;
    return true;
}

bool RedisClient::expire(const std::string &key, int ttl_seconds) const
{
    RespValue reply;
    return command(std::vector<std::string>{"EXPIRE", key, std::to_string(ttl_seconds)}, reply);
}

bool RedisClient::ttl(const std::string &key, long long &seconds) const
{
    RespValue reply;
    if (!command(std::vector<std::string>{"TTL", key}, reply) || reply.type != ':')
    {
        return false;
    }
    seconds = reply.integer;
    return true;
}

bool RedisClient::command(const std::vector<std::string> &args, RespValue &reply) const
{
    set_last_error("");
    if (!m_enabled)
    {
        set_last_error("redis is disabled");
        return false;
    }

    const int fd = connect_socket();
    if (fd < 0)
    {
        return false;
    }

    bool ok = true;
    RespValue auth_reply;
    if (!m_password.empty())
    {
        ok = send_command(fd, std::vector<std::string>{"AUTH", m_password}) && read_reply(fd, auth_reply);
    }

    RespValue select_reply;
    if (ok && m_db > 0)
    {
        ok = send_command(fd, std::vector<std::string>{"SELECT", std::to_string(m_db)}) && read_reply(fd, select_reply);
    }

    if (ok)
    {
        ok = send_command(fd, args) && read_reply(fd, reply);
    }

    close(fd);
    return ok;
}

bool RedisClient::send_command(int fd, const std::vector<std::string> &args) const
{
    std::ostringstream command;
    command << "*" << args.size() << "\r\n";
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        command << "$" << args[i].size() << "\r\n" << args[i] << "\r\n";
    }

    const std::string data = command.str();
    std::size_t sent = 0;
    while (sent < data.size())
    {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0)
        {
            set_last_error(std::string("redis send failed: ") + std::strerror(errno));
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool RedisClient::read_reply(int fd, RespValue &reply) const
{
    std::string buffer;
    char chunk[4096];

    while (true)
    {
        std::size_t offset = 0;
        if (parse_reply(buffer, offset, reply))
        {
            return reply.type != '-';
        }

        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0)
        {
            set_last_error(std::string("redis read failed: ") + std::strerror(errno));
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
    }
}

bool RedisClient::parse_reply(const std::string &buffer, std::size_t &offset, RespValue &reply) const
{
    if (offset >= buffer.size())
    {
        return false;
    }

    const char type = buffer[offset++];
    std::size_t line_end = 0;
    if (type == '+' || type == '-' || type == ':')
    {
        if (!parse_line(buffer, offset, line_end))
        {
            return false;
        }
        reply.type = type;
        reply.text = buffer.substr(offset, line_end - offset);
        reply.integer = type == ':' ? std::strtoll(reply.text.c_str(), nullptr, 10) : 0;
        reply.nil = false;
        offset = line_end + 2;
        if (type == '-')
        {
            set_last_error("redis error: " + reply.text);
        }
        return true;
    }

    if (type != '$')
    {
        set_last_error("unsupported redis reply");
        return false;
    }

    if (!parse_line(buffer, offset, line_end))
    {
        return false;
    }

    const long long length = std::strtoll(buffer.substr(offset, line_end - offset).c_str(), nullptr, 10);
    offset = line_end + 2;
    reply.type = type;
    reply.integer = 0;
    if (length < 0)
    {
        reply.nil = true;
        reply.text.clear();
        return true;
    }

    if (buffer.size() < offset + static_cast<std::size_t>(length) + 2)
    {
        return false;
    }
    reply.nil = false;
    reply.text = buffer.substr(offset, static_cast<std::size_t>(length));
    offset += static_cast<std::size_t>(length) + 2;
    return true;
}

int RedisClient::connect_socket() const
{
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *results = nullptr;
    const std::string port = std::to_string(m_port);
    const int rc = getaddrinfo(m_host.c_str(), port.c_str(), &hints, &results);
    if (rc != 0)
    {
        set_last_error(std::string("redis address lookup failed: ") + gai_strerror(rc));
        return -1;
    }

    timeval timeout;
    timeout.tv_sec = m_timeout_ms / 1000;
    timeout.tv_usec = (m_timeout_ms % 1000) * 1000;

    int fd = -1;
    for (addrinfo *candidate = results; candidate != nullptr; candidate = candidate->ai_next)
    {
        fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd < 0)
        {
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0)
        {
            freeaddrinfo(results);
            return fd;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    set_last_error(std::string("redis connect failed: ") + std::strerror(errno));
    return -1;
}

void RedisClient::set_last_error(const std::string &error) const
{
    m_last_error = error;
}
