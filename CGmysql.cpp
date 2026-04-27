#include "CGmysql.h"

#include <sstream>

CGMysqlPool::CGMysqlPool()
    : m_port(3306),
      m_max_conn(0),
      m_initialized(false)
{
}

CGMysqlPool::~CGMysqlPool()
{
    std::unique_lock<std::mutex> lock(m_mutex);
#if CGMYSQL_HAS_CLIENT
    while (!m_connections.empty())
    {
        MYSQL *conn = m_connections.front();
        m_connections.pop();
        if (conn != nullptr)
        {
            mysql_close(conn);
        }
    }
#else
    while (!m_connections.empty())
    {
        m_connections.pop();
    }
#endif
}

bool CGMysqlPool::init(const std::string &host,
                       unsigned int port,
                       const std::string &user,
                       const std::string &password,
                       const std::string &database,
                       int max_conn)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password;
    m_database = database;
    m_max_conn = max_conn > 0 ? max_conn : 1;

#if !CGMYSQL_HAS_CLIENT
    m_last_error="MySQL client headers were not found at compile time.";
    m_initialized = false;
    return false;
#else
    while (!m_connections.empty())
    {
        MYSQL *conn = m_connections.front();
        m_connections.pop();
        if (conn != nullptr)
        {
            mysql_close(conn);
        }
    }

    for (int i = 0; i < m_max_conn; ++i)
    {
        MYSQL *conn = mysql_init(nullptr);
        if (conn == nullptr)
        {
            m_last_error="mysql_init failed";
            m_initialized = false;
            return false;
        }

        if (mysql_real_connect(conn,
                               m_host.c_str(),
                               m_user.c_str(),
                               m_password.c_str(),
                               m_database.c_str(),
                               m_port,
                               nullptr,
                               0) == nullptr)
        {
            std::ostringstream message;
            message << "mysql_real_connect failed: " << mysql_error(conn);
            m_last_error=message.str();
            mysql_close(conn);
            m_initialized = false;
            return false;
        }

        mysql_set_character_set(conn, "utf8mb4");
        m_connections.push(conn);
    }

    m_last_error.clear();
    m_initialized = true;
    return true;
#endif
}

MYSQL *CGMysqlPool::get_connection()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_initialized)
    {
        return nullptr;
    }

    while (m_connections.empty())
    {
        m_condition.wait(lock);
    }

    MYSQL *conn = m_connections.front();
    m_connections.pop();
    return conn;
}

void CGMysqlPool::release_connection(MYSQL *conn)
{
    if (conn == nullptr)
    {
        return;
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_connections.push(conn);
    lock.unlock();
    m_condition.notify_one();
}

bool CGMysqlPool::available() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_initialized;
}

std::string CGMysqlPool::last_error() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_last_error;
}

bool CGMysqlPool::fetch_user_password(const std::string &username, std::string &password_out)
{
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)password_out;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    const std::string escaped_user = escape_string(conn, username);
    const std::string query =
        "SELECT passwd FROM user WHERE username='" + escaped_user + "' LIMIT 1";

    if (mysql_query(conn, query.c_str()) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        return false;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    if (result == nullptr)
    {
        set_error("mysql_store_result returned null.");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr || row[0] == nullptr)
    {
        mysql_free_result(result);
        set_error("User was not found in table `user`.");
        return false;
    }

    password_out = row[0];
    mysql_free_result(result);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::user_exists(const std::string &username, bool &exists_out)
{
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    exists_out = false;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    exists_out = false;
    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    const std::string escaped_user = escape_string(conn, username);
    const std::string query =
        "SELECT 1 FROM user WHERE username='" + escaped_user + "' LIMIT 1";

    if (mysql_query(conn, query.c_str()) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        return false;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    if (result == nullptr)
    {
        set_error("mysql_store_result returned null.");
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    exists_out = (row != nullptr);
    mysql_free_result(result);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::insert_user(const std::string &username, const std::string &password)
{
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)password;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    const std::string escaped_user = escape_string(conn, username);
    const std::string escaped_password = escape_string(conn, password);
    const std::string query =
        "INSERT INTO user(username, passwd) VALUES('" + escaped_user + "','" + escaped_password + "')";

    if (mysql_query(conn, query.c_str()) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

void CGMysqlPool::set_error(const std::string &message)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_last_error = message;
}

std::string CGMysqlPool::escape_string(MYSQL *conn, const std::string &value)
{
#if !CGMYSQL_HAS_CLIENT
    (void)conn;
    return value;
#else
    std::string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long size = mysql_real_escape_string(conn, &escaped[0], value.c_str(), static_cast<unsigned long>(value.size()));
    escaped.resize(size);
    return escaped;
#endif
}

CGMysqlGuard::CGMysqlGuard(CGMysqlPool &pool)
    : m_pool(pool),
      m_conn(pool.get_connection())
{
}

CGMysqlGuard::~CGMysqlGuard()
{
    m_pool.release_connection(m_conn);
}

MYSQL *CGMysqlGuard::get() const
{
    return m_conn;
}

bool CGMysqlGuard::valid() const
{
    return m_conn != nullptr;
}
