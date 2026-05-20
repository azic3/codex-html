#include "CGmysql.h"

#include <cstring>
#include <sstream>
#include <vector>

#if CGMYSQL_HAS_CLIENT
namespace
{
#if defined(MARIADB_BASE_VERSION) || (defined(LIBMYSQL_VERSION_ID) && LIBMYSQL_VERSION_ID < 80000)
typedef my_bool MysqlBool;
#else
typedef bool MysqlBool;
#endif

void bind_string_param(MYSQL_BIND &bind, const std::string &value, unsigned long &length)
{
    std::memset(&bind, 0, sizeof(bind));
    length = static_cast<unsigned long>(value.size());
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char *>(value.data());
    bind.buffer_length = length;
    bind.length = &length;
}

bool execute_prepared(MYSQL *conn,
                      const char *sql,
                      MYSQL_BIND *params,
                      const unsigned long param_count,
                      std::string &error_out)
{
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        error_out = "mysql_stmt_init failed.";
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    if (param_count > 0 && mysql_stmt_bind_param(stmt, params) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    error_out.clear();
    return true;
}

bool query_column_exists(MYSQL *conn, const std::string &database, const char *column_name, bool &exists_out, std::string &error_out)
{
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        error_out = "mysql_stmt_init failed.";
        return false;
    }

    const char *sql =
        "SELECT 1 FROM INFORMATION_SCHEMA.COLUMNS "
        "WHERE TABLE_SCHEMA=? AND TABLE_NAME='user' AND COLUMN_NAME=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[2];
    unsigned long database_length = 0;
    unsigned long column_length = 0;
    const std::string column(column_name);
    bind_string_param(param[0], database, database_length);
    bind_string_param(param[1], column, column_length);
    if (mysql_stmt_bind_param(stmt, param) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0 || mysql_stmt_store_result(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    const int fetch_status = mysql_stmt_fetch(stmt);
    if (fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(stmt);
        error_out = message.str();
        mysql_stmt_close(stmt);
        return false;
    }

    exists_out = (fetch_status != MYSQL_NO_DATA);
    mysql_stmt_close(stmt);
    error_out.clear();
    return true;
}

bool ensure_password_version_column(MYSQL *conn, const std::string &database, std::string &error_out)
{
    bool exists = false;
    if (!query_column_exists(conn, database, "password_version", exists, error_out))
    {
        return false;
    }

    if (exists)
    {
        return true;
    }

    const char *sql = "ALTER TABLE user ADD COLUMN password_version VARCHAR(32) NOT NULL DEFAULT 'legacy'";
    if (mysql_query(conn, sql) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        error_out = message.str();
        return false;
    }

    error_out.clear();
    return true;
}
}
#endif

CGMysqlPool::CGMysqlPool()
    : m_port(3306),
      m_max_conn(0),
      m_initialized(false),
      m_password_version_supported(false)
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
    m_password_version_supported = false;

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
        if (i == 0)
        {
            std::string schema_error;
            m_password_version_supported = ensure_password_version_column(conn, m_database, schema_error);
        }
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
    std::string password_version;
    return fetch_user_credentials(username, password_out, password_version);
}

bool CGMysqlPool::fetch_user_credentials(const std::string &username,
                                         std::string &password_out,
                                         std::string &password_version_out)
{
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)password_out;
    (void)password_version_out;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    password_out.clear();
    password_version_out.clear();
    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const bool with_version = m_password_version_supported;
    const char *sql = with_version
                          ? "SELECT passwd, password_version FROM user WHERE username=? LIMIT 1"
                          : "SELECT passwd FROM user WHERE username=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[1];
    unsigned long username_length = 0;
    bind_string_param(param[0], username, username_length);
    if (mysql_stmt_bind_param(stmt, param) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    std::vector<char> password_buffer(4096);
    std::vector<char> version_buffer(64);
    unsigned long password_length = 0;
    unsigned long version_length = 0;
    MysqlBool password_is_null = 0;
    MysqlBool version_is_null = 0;
    MYSQL_BIND result[2];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = password_buffer.data();
    result[0].buffer_length = static_cast<unsigned long>(password_buffer.size());
    result[0].length = &password_length;
    result[0].is_null = &password_is_null;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = version_buffer.data();
    result[1].buffer_length = static_cast<unsigned long>(version_buffer.size());
    result[1].length = &version_length;
    result[1].is_null = &version_is_null;

    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_store_result(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_store_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    const int fetch_status = mysql_stmt_fetch(stmt);
    if (fetch_status == MYSQL_NO_DATA || password_is_null)
    {
        set_error("User was not found in table `user`.");
        mysql_stmt_close(stmt);
        return false;
    }
    if (fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    if (fetch_status == MYSQL_DATA_TRUNCATED && password_length > password_buffer.size())
    {
        password_buffer.resize(password_length);
        result[0].buffer = password_buffer.data();
        result[0].buffer_length = password_length;
        if (mysql_stmt_fetch_column(stmt, &result[0], 0, 0) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_fetch_column failed: " << mysql_stmt_error(stmt);
            set_error(message.str());
            mysql_stmt_close(stmt);
            return false;
        }
    }

    password_out.assign(password_buffer.data(), password_length);
    if (with_version && !version_is_null)
    {
        password_version_out.assign(version_buffer.data(), version_length);
    }
    mysql_stmt_close(stmt);
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
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *sql = "SELECT 1 FROM user WHERE username=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[1];
    unsigned long username_length = 0;
    bind_string_param(param[0], username, username_length);
    if (mysql_stmt_bind_param(stmt, param) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    int found_marker = 0;
    unsigned long found_marker_length = 0;
    MysqlBool found_marker_is_null = 0;
    MYSQL_BIND result[1];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &found_marker;
    result[0].buffer_length = sizeof(found_marker);
    result[0].length = &found_marker_length;
    result[0].is_null = &found_marker_is_null;

    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_store_result(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_store_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    const int fetch_status = mysql_stmt_fetch(stmt);
    if (fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    exists_out = (fetch_status != MYSQL_NO_DATA);
    mysql_stmt_close(stmt);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::insert_user(const std::string &username, const std::string &password)
{
    return insert_user(username, password, "legacy");
}

bool CGMysqlPool::insert_user(const std::string &username, const std::string &password, const std::string &password_version)
{
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)password;
    (void)password_version;
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
    MYSQL_BIND param[3];
    unsigned long username_length = 0;
    unsigned long password_length = 0;
    unsigned long version_length = 0;
    bind_string_param(param[0], username, username_length);
    bind_string_param(param[1], password, password_length);
    bind_string_param(param[2], password_version, version_length);

    std::string error;
    if (!execute_prepared(conn,
                          m_password_version_supported
                              ? "INSERT INTO user(username, passwd, password_version) VALUES(?, ?, ?)"
                              : "INSERT INTO user(username, passwd) VALUES(?, ?)",
                          param,
                          m_password_version_supported ? 3 : 2,
                          error))
    {
        set_error(error);
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::update_user_password(const std::string &username, const std::string &password)
{
    return update_user_password(username, password, "legacy");
}

bool CGMysqlPool::update_user_password(const std::string &username, const std::string &password, const std::string &password_version)
{
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)password;
    (void)password_version;
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
    MYSQL_BIND param[3];
    unsigned long password_length = 0;
    unsigned long username_length = 0;
    unsigned long version_length = 0;
    bind_string_param(param[0], password, password_length);
    if (m_password_version_supported)
    {
        bind_string_param(param[1], password_version, version_length);
        bind_string_param(param[2], username, username_length);
    }
    else
    {
        bind_string_param(param[1], username, username_length);
    }

    std::string error;
    if (!execute_prepared(conn,
                          m_password_version_supported
                              ? "UPDATE user SET passwd=?, password_version=? WHERE username=? LIMIT 1"
                              : "UPDATE user SET passwd=? WHERE username=? LIMIT 1",
                          param,
                          m_password_version_supported ? 3 : 2,
                          error))
    {
        set_error(error);
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
