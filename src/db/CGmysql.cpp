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

bool ensure_images_table(MYSQL *conn, std::string &error_out)
{
    const char *images_sql =
        "CREATE TABLE IF NOT EXISTS images ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "filename VARCHAR(255) NOT NULL,"
        "url VARCHAR(512) NOT NULL,"
        "uploader VARCHAR(64) DEFAULT NULL,"
        "size BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "like_count BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "comment_count BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "favorite_count BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "download_count BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY (id),"
        "UNIQUE KEY uk_images_filename (filename),"
        "KEY idx_images_created_at (created_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn, images_sql) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        error_out = message.str();
        return false;
    }

    const char *likes_sql =
        "CREATE TABLE IF NOT EXISTS image_likes ("
        "image_id BIGINT UNSIGNED NOT NULL,"
        "username VARCHAR(64) NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY (image_id, username),"
        "KEY idx_image_likes_username (username),"
        "CONSTRAINT fk_image_likes_image_id FOREIGN KEY (image_id) REFERENCES images(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn, likes_sql) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        error_out = message.str();
        return false;
    }

    const char *favorites_sql =
        "CREATE TABLE IF NOT EXISTS image_favorites ("
        "image_id BIGINT UNSIGNED NOT NULL,"
        "username VARCHAR(64) NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY (image_id, username),"
        "KEY idx_image_favorites_username (username),"
        "CONSTRAINT fk_image_favorites_image_id FOREIGN KEY (image_id) REFERENCES images(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn, favorites_sql) != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        error_out = message.str();
        return false;
    }

    const char *comments_sql =
        "CREATE TABLE IF NOT EXISTS image_comments ("
        "id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "image_id BIGINT UNSIGNED NOT NULL,"
        "username VARCHAR(64) NOT NULL,"
        "content VARCHAR(300) NOT NULL,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "deleted_at TIMESTAMP NULL DEFAULT NULL,"
        "PRIMARY KEY (id),"
        "KEY idx_image_comments_image_id_created_at (image_id, created_at),"
        "KEY idx_image_comments_username (username),"
        "CONSTRAINT fk_image_comments_image_id FOREIGN KEY (image_id) REFERENCES images(id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn, comments_sql) != 0)
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
      m_password_version_supported(false),
      m_images_table_supported(false)
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
    m_images_table_supported = false;

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
            m_images_table_supported = ensure_images_table(conn, schema_error);
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

bool CGMysqlPool::insert_image(const std::string &filename,
                               const std::string &url,
                               const std::string &uploader,
                               unsigned long long size,
                               unsigned long long &id_out)
{
    id_out = 0;
#if !CGMYSQL_HAS_CLIENT
    (void)filename;
    (void)url;
    (void)uploader;
    (void)size;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }

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

    const char *sql =
        "INSERT INTO images(filename, url, uploader, size) VALUES(?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE "
        "url=VALUES(url), uploader=VALUES(uploader), size=VALUES(size), updated_at=CURRENT_TIMESTAMP";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[4];
    std::memset(param, 0, sizeof(param));
    unsigned long filename_length = 0;
    unsigned long url_length = 0;
    unsigned long uploader_length = 0;
    bind_string_param(param[0], filename, filename_length);
    bind_string_param(param[1], url, url_length);
    bind_string_param(param[2], uploader, uploader_length);
    param[3].buffer_type = MYSQL_TYPE_LONGLONG;
    param[3].buffer = &size;
    param[3].buffer_length = sizeof(size);
    param[3].is_unsigned = 1;

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

    mysql_stmt_close(stmt);
    id_out = static_cast<unsigned long long>(mysql_insert_id(conn));
    if (id_out == 0)
    {
        MYSQL_STMT *select_stmt = mysql_stmt_init(conn);
        if (select_stmt == nullptr)
        {
            set_error("mysql_stmt_init failed.");
            return false;
        }

        const char *select_sql = "SELECT id FROM images WHERE filename=? LIMIT 1";
        if (mysql_stmt_prepare(select_stmt, select_sql, static_cast<unsigned long>(std::strlen(select_sql))) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_prepare failed: " << mysql_stmt_error(select_stmt);
            set_error(message.str());
            mysql_stmt_close(select_stmt);
            return false;
        }

        MYSQL_BIND select_param[1];
        unsigned long select_filename_length = 0;
        bind_string_param(select_param[0], filename, select_filename_length);
        if (mysql_stmt_bind_param(select_stmt, select_param) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(select_stmt);
            set_error(message.str());
            mysql_stmt_close(select_stmt);
            return false;
        }

        if (mysql_stmt_execute(select_stmt) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_execute failed: " << mysql_stmt_error(select_stmt);
            set_error(message.str());
            mysql_stmt_close(select_stmt);
            return false;
        }

        unsigned long id_length = 0;
        MysqlBool id_is_null = 0;
        MYSQL_BIND result[1];
        std::memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result[0].buffer = &id_out;
        result[0].buffer_length = sizeof(id_out);
        result[0].length = &id_length;
        result[0].is_null = &id_is_null;
        result[0].is_unsigned = 1;
        if (mysql_stmt_bind_result(select_stmt, result) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(select_stmt);
            set_error(message.str());
            mysql_stmt_close(select_stmt);
            return false;
        }

        const int fetch_status = mysql_stmt_fetch(select_stmt);
        if (fetch_status == MYSQL_NO_DATA || id_is_null)
        {
            set_error("Image was not found after insert.");
            mysql_stmt_close(select_stmt);
            return false;
        }
        if (fetch_status == 1)
        {
            std::ostringstream message;
            message << "mysql_stmt_fetch failed: " << mysql_stmt_error(select_stmt);
            set_error(message.str());
            mysql_stmt_close(select_stmt);
            return false;
        }
        mysql_stmt_close(select_stmt);
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::fetch_images_page(std::size_t page,
                                    std::size_t limit,
                                    const std::string &username,
                                    std::vector<ImageRecord> &images_out,
                                    unsigned long long &total_out)
{
    images_out.clear();
    total_out = 0;
#if !CGMYSQL_HAS_CLIENT
    (void)page;
    (void)limit;
    (void)username;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }

    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    MYSQL_STMT *count_stmt = mysql_stmt_init(conn);
    if (count_stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *count_sql = "SELECT COUNT(*) FROM images";
    if (mysql_stmt_prepare(count_stmt, count_sql, static_cast<unsigned long>(std::strlen(count_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }

    if (mysql_stmt_execute(count_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }

    unsigned long total_length = 0;
    MysqlBool total_is_null = 0;
    MYSQL_BIND count_result[1];
    std::memset(count_result, 0, sizeof(count_result));
    count_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_result[0].buffer = &total_out;
    count_result[0].buffer_length = sizeof(total_out);
    count_result[0].length = &total_length;
    count_result[0].is_null = &total_is_null;
    count_result[0].is_unsigned = 1;
    if (mysql_stmt_bind_result(count_stmt, count_result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }

    const int count_fetch_status = mysql_stmt_fetch(count_stmt);
    if (count_fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    mysql_stmt_close(count_stmt);

    unsigned long long offset = page > 0 ? static_cast<unsigned long long>((page - 1) * limit) : 0;
    unsigned long long limit_value = static_cast<unsigned long long>(limit);
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *sql =
        "SELECT id, filename, url, COALESCE(uploader, ''), size, "
        "like_count, comment_count, favorite_count, download_count, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s'), "
        "EXISTS(SELECT 1 FROM image_likes WHERE image_likes.image_id=images.id AND image_likes.username=?), "
        "EXISTS(SELECT 1 FROM image_favorites WHERE image_favorites.image_id=images.id AND image_favorites.username=?) "
        "FROM images ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[4];
    std::memset(param, 0, sizeof(param));
    unsigned long username_length = 0;
    unsigned long username_length_again = 0;
    bind_string_param(param[0], username, username_length);
    bind_string_param(param[1], username, username_length_again);
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &limit_value;
    param[2].buffer_length = sizeof(limit_value);
    param[2].is_unsigned = 1;
    param[3].buffer_type = MYSQL_TYPE_LONGLONG;
    param[3].buffer = &offset;
    param[3].buffer_length = sizeof(offset);
    param[3].is_unsigned = 1;
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

    if (mysql_stmt_store_result(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_store_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    ImageRecord row;
    std::vector<char> filename_buffer(1024);
    std::vector<char> url_buffer(2048);
    std::vector<char> uploader_buffer(256);
    std::vector<char> created_buffer(32);
    unsigned long lengths[12] = {0};
    MysqlBool nulls[12] = {0};
    MYSQL_BIND result[12];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &row.id;
    result[0].buffer_length = sizeof(row.id);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = filename_buffer.data();
    result[1].buffer_length = static_cast<unsigned long>(filename_buffer.size());
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = url_buffer.data();
    result[2].buffer_length = static_cast<unsigned long>(url_buffer.size());
    result[2].length = &lengths[2];
    result[2].is_null = &nulls[2];
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = uploader_buffer.data();
    result[3].buffer_length = static_cast<unsigned long>(uploader_buffer.size());
    result[3].length = &lengths[3];
    result[3].is_null = &nulls[3];
    result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result[4].buffer = &row.size;
    result[4].buffer_length = sizeof(row.size);
    result[4].length = &lengths[4];
    result[4].is_null = &nulls[4];
    result[4].is_unsigned = 1;
    result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    result[5].buffer = &row.like_count;
    result[5].buffer_length = sizeof(row.like_count);
    result[5].length = &lengths[5];
    result[5].is_null = &nulls[5];
    result[5].is_unsigned = 1;
    result[6].buffer_type = MYSQL_TYPE_LONGLONG;
    result[6].buffer = &row.comment_count;
    result[6].buffer_length = sizeof(row.comment_count);
    result[6].length = &lengths[6];
    result[6].is_null = &nulls[6];
    result[6].is_unsigned = 1;
    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &row.favorite_count;
    result[7].buffer_length = sizeof(row.favorite_count);
    result[7].length = &lengths[7];
    result[7].is_null = &nulls[7];
    result[7].is_unsigned = 1;
    result[8].buffer_type = MYSQL_TYPE_LONGLONG;
    result[8].buffer = &row.download_count;
    result[8].buffer_length = sizeof(row.download_count);
    result[8].length = &lengths[8];
    result[8].is_null = &nulls[8];
    result[8].is_unsigned = 1;
    result[9].buffer_type = MYSQL_TYPE_STRING;
    result[9].buffer = created_buffer.data();
    result[9].buffer_length = static_cast<unsigned long>(created_buffer.size());
    result[9].length = &lengths[9];
    result[9].is_null = &nulls[9];
    int liked_marker = 0;
    int favorited_marker = 0;
    result[10].buffer_type = MYSQL_TYPE_LONG;
    result[10].buffer = &liked_marker;
    result[10].buffer_length = sizeof(liked_marker);
    result[10].length = &lengths[10];
    result[10].is_null = &nulls[10];
    result[11].buffer_type = MYSQL_TYPE_LONG;
    result[11].buffer = &favorited_marker;
    result[11].buffer_length = sizeof(favorited_marker);
    result[11].length = &lengths[11];
    result[11].is_null = &nulls[11];

    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    while (true)
    {
        std::memset(nulls, 0, sizeof(nulls));
        row = ImageRecord();
        liked_marker = 0;
        favorited_marker = 0;
        const int fetch_status = mysql_stmt_fetch(stmt);
        if (fetch_status == MYSQL_NO_DATA)
        {
            break;
        }
        if (fetch_status == 1)
        {
            std::ostringstream message;
            message << "mysql_stmt_fetch failed: " << mysql_stmt_error(stmt);
            set_error(message.str());
            mysql_stmt_close(stmt);
            return false;
        }

        row.filename.assign(filename_buffer.data(), lengths[1]);
        row.url.assign(url_buffer.data(), lengths[2]);
        row.uploader.assign(uploader_buffer.data(), lengths[3]);
        row.created_at.assign(created_buffer.data(), lengths[9]);
        row.liked = liked_marker != 0;
        row.favorited = favorited_marker != 0;
        images_out.push_back(row);
    }

    mysql_stmt_close(stmt);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::set_image_like(unsigned long long image_id,
                                 const std::string &username,
                                 bool active,
                                 ImageReactionState &state_out)
{
    return set_image_reaction(image_id, username, active, false, state_out);
}

bool CGMysqlPool::set_image_favorite(unsigned long long image_id,
                                     const std::string &username,
                                     bool active,
                                     ImageReactionState &state_out)
{
    return set_image_reaction(image_id, username, active, true, state_out);
}

bool CGMysqlPool::set_image_reaction(unsigned long long image_id,
                                     const std::string &username,
                                     bool active,
                                     bool favorite,
                                     ImageReactionState &state_out)
{
    state_out = ImageReactionState();
    state_out.image_id = image_id;
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)active;
    (void)favorite;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (image_id == 0 || username.empty())
    {
        set_error("Invalid image reaction request.");
        return false;
    }

    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    const char *table = favorite ? "image_favorites" : "image_likes";
    const char *count_column = favorite ? "favorite_count" : "like_count";
    std::string reaction_sql = active
                                   ? std::string("INSERT IGNORE INTO ") + table + "(image_id, username) VALUES(?, ?)"
                                   : std::string("DELETE FROM ") + table + " WHERE image_id=? AND username=?";

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    if (mysql_stmt_prepare(stmt, reaction_sql.c_str(), static_cast<unsigned long>(reaction_sql.size())) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[2];
    std::memset(param, 0, sizeof(param));
    unsigned long username_length = 0;
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &image_id;
    param[0].buffer_length = sizeof(image_id);
    param[0].is_unsigned = 1;
    bind_string_param(param[1], username, username_length);
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

    const bool changed = mysql_stmt_affected_rows(stmt) > 0;
    mysql_stmt_close(stmt);

    if (changed)
    {
        std::string count_sql = active
                                    ? std::string("UPDATE images SET ") + count_column + "=" + count_column + "+1 WHERE id=? LIMIT 1"
                                    : std::string("UPDATE images SET ") + count_column + "=IF(" + count_column + ">0," + count_column + "-1,0) WHERE id=? LIMIT 1";
        MYSQL_STMT *count_stmt = mysql_stmt_init(conn);
        if (count_stmt == nullptr)
        {
            set_error("mysql_stmt_init failed.");
            return false;
        }
        if (mysql_stmt_prepare(count_stmt, count_sql.c_str(), static_cast<unsigned long>(count_sql.size())) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_prepare failed: " << mysql_stmt_error(count_stmt);
            set_error(message.str());
            mysql_stmt_close(count_stmt);
            return false;
        }

        MYSQL_BIND count_param[1];
        std::memset(count_param, 0, sizeof(count_param));
        count_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
        count_param[0].buffer = &image_id;
        count_param[0].buffer_length = sizeof(image_id);
        count_param[0].is_unsigned = 1;
        if (mysql_stmt_bind_param(count_stmt, count_param) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(count_stmt);
            set_error(message.str());
            mysql_stmt_close(count_stmt);
            return false;
        }
        if (mysql_stmt_execute(count_stmt) != 0)
        {
            std::ostringstream message;
            message << "mysql_stmt_execute failed: " << mysql_stmt_error(count_stmt);
            set_error(message.str());
            mysql_stmt_close(count_stmt);
            return false;
        }
        mysql_stmt_close(count_stmt);
    }

    MYSQL_STMT *select_stmt = mysql_stmt_init(conn);
    if (select_stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *select_sql =
        "SELECT like_count, favorite_count, "
        "EXISTS(SELECT 1 FROM image_likes WHERE image_likes.image_id=images.id AND image_likes.username=?), "
        "EXISTS(SELECT 1 FROM image_favorites WHERE image_favorites.image_id=images.id AND image_favorites.username=?) "
        "FROM images WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(select_stmt, select_sql, static_cast<unsigned long>(std::strlen(select_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }

    MYSQL_BIND select_param[3];
    std::memset(select_param, 0, sizeof(select_param));
    unsigned long username_length_one = 0;
    unsigned long username_length_two = 0;
    bind_string_param(select_param[0], username, username_length_one);
    bind_string_param(select_param[1], username, username_length_two);
    select_param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    select_param[2].buffer = &image_id;
    select_param[2].buffer_length = sizeof(image_id);
    select_param[2].is_unsigned = 1;
    if (mysql_stmt_bind_param(select_stmt, select_param) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }
    if (mysql_stmt_execute(select_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }

    int liked_marker = 0;
    int favorited_marker = 0;
    unsigned long lengths[4] = {0};
    MysqlBool nulls[4] = {0};
    MYSQL_BIND result[4];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &state_out.like_count;
    result[0].buffer_length = sizeof(state_out.like_count);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &state_out.favorite_count;
    result[1].buffer_length = sizeof(state_out.favorite_count);
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    result[1].is_unsigned = 1;
    result[2].buffer_type = MYSQL_TYPE_LONG;
    result[2].buffer = &liked_marker;
    result[2].buffer_length = sizeof(liked_marker);
    result[2].length = &lengths[2];
    result[2].is_null = &nulls[2];
    result[3].buffer_type = MYSQL_TYPE_LONG;
    result[3].buffer = &favorited_marker;
    result[3].buffer_length = sizeof(favorited_marker);
    result[3].length = &lengths[3];
    result[3].is_null = &nulls[3];
    if (mysql_stmt_bind_result(select_stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }

    const int fetch_status = mysql_stmt_fetch(select_stmt);
    if (fetch_status == MYSQL_NO_DATA)
    {
        set_error("Image was not found.");
        mysql_stmt_close(select_stmt);
        return false;
    }
    if (fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }
    mysql_stmt_close(select_stmt);

    state_out.liked = liked_marker != 0;
    state_out.favorited = favorited_marker != 0;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::insert_image_comment(unsigned long long image_id,
                                       const std::string &username,
                                       const std::string &content,
                                       ImageCommentRecord &comment_out)
{
    comment_out = ImageCommentRecord();
#if !CGMYSQL_HAS_CLIENT
    (void)image_id;
    (void)username;
    (void)content;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (image_id == 0 || username.empty() || content.empty())
    {
        set_error("Invalid image comment request.");
        return false;
    }

    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    if (mysql_query(conn, "START TRANSACTION") != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        return false;
    }

    MYSQL_STMT *count_stmt = mysql_stmt_init(conn);
    if (count_stmt == nullptr)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *count_sql = "UPDATE images SET comment_count=comment_count+1 WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(count_stmt, count_sql, static_cast<unsigned long>(std::strlen(count_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }

    MYSQL_BIND count_param[1];
    std::memset(count_param, 0, sizeof(count_param));
    count_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_param[0].buffer = &image_id;
    count_param[0].buffer_length = sizeof(image_id);
    count_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(count_stmt, count_param) != 0 || mysql_stmt_execute(count_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    const bool image_updated = mysql_stmt_affected_rows(count_stmt) > 0;
    mysql_stmt_close(count_stmt);
    if (!image_updated)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("Image was not found.");
        return false;
    }

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *sql = "INSERT INTO image_comments(image_id, username, content) VALUES(?, ?, ?)";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }

    MYSQL_BIND param[3];
    std::memset(param, 0, sizeof(param));
    unsigned long username_length = 0;
    unsigned long content_length = 0;
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &image_id;
    param[0].buffer_length = sizeof(image_id);
    param[0].is_unsigned = 1;
    bind_string_param(param[1], username, username_length);
    bind_string_param(param[2], content, content_length);
    if (mysql_stmt_bind_param(stmt, param) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    mysql_stmt_close(stmt);

    const unsigned long long comment_id = static_cast<unsigned long long>(mysql_insert_id(conn));
    MYSQL_STMT *select_stmt = mysql_stmt_init(conn);
    if (select_stmt == nullptr)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *select_sql =
        "SELECT id, image_id, username, content, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM image_comments WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(select_stmt, select_sql, static_cast<unsigned long>(std::strlen(select_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }

    MYSQL_BIND select_param[1];
    std::memset(select_param, 0, sizeof(select_param));
    unsigned long long selected_id = comment_id;
    select_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    select_param[0].buffer = &selected_id;
    select_param[0].buffer_length = sizeof(selected_id);
    select_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(select_stmt, select_param) != 0 || mysql_stmt_execute(select_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }

    std::vector<char> username_buffer(256);
    std::vector<char> content_buffer(1024);
    std::vector<char> created_buffer(32);
    unsigned long lengths[5] = {0};
    MysqlBool nulls[5] = {0};
    MYSQL_BIND result[5];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &comment_out.id;
    result[0].buffer_length = sizeof(comment_out.id);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &comment_out.image_id;
    result[1].buffer_length = sizeof(comment_out.image_id);
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    result[1].is_unsigned = 1;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = username_buffer.data();
    result[2].buffer_length = static_cast<unsigned long>(username_buffer.size());
    result[2].length = &lengths[2];
    result[2].is_null = &nulls[2];
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = content_buffer.data();
    result[3].buffer_length = static_cast<unsigned long>(content_buffer.size());
    result[3].length = &lengths[3];
    result[3].is_null = &nulls[3];
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = created_buffer.data();
    result[4].buffer_length = static_cast<unsigned long>(created_buffer.size());
    result[4].length = &lengths[4];
    result[4].is_null = &nulls[4];
    if (mysql_stmt_bind_result(select_stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    const int fetch_status = mysql_stmt_fetch(select_stmt);
    if (fetch_status == MYSQL_NO_DATA || fetch_status == 1)
    {
        std::ostringstream message;
        message << (fetch_status == MYSQL_NO_DATA ? "Comment was not found after insert." : mysql_stmt_error(select_stmt));
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    comment_out.username.assign(username_buffer.data(), lengths[2]);
    comment_out.content.assign(content_buffer.data(), lengths[3]);
    comment_out.created_at.assign(created_buffer.data(), lengths[4]);
    mysql_stmt_close(select_stmt);

    if (mysql_query(conn, "COMMIT") != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        mysql_query(conn, "ROLLBACK");
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::fetch_image_comments(unsigned long long image_id,
                                       std::size_t page,
                                       std::size_t limit,
                                       std::vector<ImageCommentRecord> &comments_out,
                                       unsigned long long &total_out)
{
    comments_out.clear();
    total_out = 0;
#if !CGMYSQL_HAS_CLIENT
    (void)image_id;
    (void)page;
    (void)limit;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (image_id == 0)
    {
        set_error("Invalid image id.");
        return false;
    }

    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    MYSQL_STMT *count_stmt = mysql_stmt_init(conn);
    if (count_stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }
    const char *count_sql = "SELECT COUNT(*) FROM image_comments WHERE image_id=? AND deleted_at IS NULL";
    if (mysql_stmt_prepare(count_stmt, count_sql, static_cast<unsigned long>(std::strlen(count_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    MYSQL_BIND count_param[1];
    std::memset(count_param, 0, sizeof(count_param));
    count_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_param[0].buffer = &image_id;
    count_param[0].buffer_length = sizeof(image_id);
    count_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(count_stmt, count_param) != 0 || mysql_stmt_execute(count_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    unsigned long total_length = 0;
    MysqlBool total_is_null = 0;
    MYSQL_BIND count_result[1];
    std::memset(count_result, 0, sizeof(count_result));
    count_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_result[0].buffer = &total_out;
    count_result[0].buffer_length = sizeof(total_out);
    count_result[0].length = &total_length;
    count_result[0].is_null = &total_is_null;
    count_result[0].is_unsigned = 1;
    if (mysql_stmt_bind_result(count_stmt, count_result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    const int count_fetch_status = mysql_stmt_fetch(count_stmt);
    if (count_fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    mysql_stmt_close(count_stmt);

    unsigned long long offset = page > 0 ? static_cast<unsigned long long>((page - 1) * limit) : 0;
    unsigned long long limit_value = static_cast<unsigned long long>(limit);
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }
    const char *sql =
        "SELECT id, image_id, username, content, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM image_comments WHERE image_id=? AND deleted_at IS NULL "
        "ORDER BY created_at DESC, id DESC LIMIT ? OFFSET ?";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }
    MYSQL_BIND param[3];
    std::memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &image_id;
    param[0].buffer_length = sizeof(image_id);
    param[0].is_unsigned = 1;
    param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    param[1].buffer = &limit_value;
    param[1].buffer_length = sizeof(limit_value);
    param[1].is_unsigned = 1;
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &offset;
    param[2].buffer_length = sizeof(offset);
    param[2].is_unsigned = 1;
    if (mysql_stmt_bind_param(stmt, param) != 0 || mysql_stmt_execute(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
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

    ImageCommentRecord row;
    std::vector<char> username_buffer(256);
    std::vector<char> content_buffer(1024);
    std::vector<char> created_buffer(32);
    unsigned long lengths[5] = {0};
    MysqlBool nulls[5] = {0};
    MYSQL_BIND result[5];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &row.id;
    result[0].buffer_length = sizeof(row.id);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &row.image_id;
    result[1].buffer_length = sizeof(row.image_id);
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    result[1].is_unsigned = 1;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = username_buffer.data();
    result[2].buffer_length = static_cast<unsigned long>(username_buffer.size());
    result[2].length = &lengths[2];
    result[2].is_null = &nulls[2];
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = content_buffer.data();
    result[3].buffer_length = static_cast<unsigned long>(content_buffer.size());
    result[3].length = &lengths[3];
    result[3].is_null = &nulls[3];
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = created_buffer.data();
    result[4].buffer_length = static_cast<unsigned long>(created_buffer.size());
    result[4].length = &lengths[4];
    result[4].is_null = &nulls[4];
    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    while (true)
    {
        std::memset(nulls, 0, sizeof(nulls));
        row = ImageCommentRecord();
        const int fetch_status = mysql_stmt_fetch(stmt);
        if (fetch_status == MYSQL_NO_DATA)
        {
            break;
        }
        if (fetch_status == 1)
        {
            std::ostringstream message;
            message << "mysql_stmt_fetch failed: " << mysql_stmt_error(stmt);
            set_error(message.str());
            mysql_stmt_close(stmt);
            return false;
        }
        row.username.assign(username_buffer.data(), lengths[2]);
        row.content.assign(content_buffer.data(), lengths[3]);
        row.created_at.assign(created_buffer.data(), lengths[4]);
        comments_out.push_back(row);
    }
    mysql_stmt_close(stmt);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::fetch_user_favorites(const std::string &username,
                                       std::size_t page,
                                       std::size_t limit,
                                       std::vector<FavoriteImageRecord> &favorites_out,
                                       unsigned long long &total_out)
{
    favorites_out.clear();
    total_out = 0;
#if !CGMYSQL_HAS_CLIENT
    (void)username;
    (void)page;
    (void)limit;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (username.empty())
    {
        set_error("Invalid favorites request.");
        return false;
    }

    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    MYSQL_STMT *count_stmt = mysql_stmt_init(conn);
    if (count_stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *count_sql =
        "SELECT COUNT(*) FROM image_favorites "
        "JOIN images ON image_favorites.image_id=images.id "
        "WHERE image_favorites.username=?";
    if (mysql_stmt_prepare(count_stmt, count_sql, static_cast<unsigned long>(std::strlen(count_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }

    MYSQL_BIND count_param[1];
    std::memset(count_param, 0, sizeof(count_param));
    unsigned long count_username_length = 0;
    bind_string_param(count_param[0], username, count_username_length);
    if (mysql_stmt_bind_param(count_stmt, count_param) != 0 || mysql_stmt_execute(count_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }

    unsigned long total_length = 0;
    MysqlBool total_is_null = 0;
    MYSQL_BIND count_result[1];
    std::memset(count_result, 0, sizeof(count_result));
    count_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_result[0].buffer = &total_out;
    count_result[0].buffer_length = sizeof(total_out);
    count_result[0].length = &total_length;
    count_result[0].is_null = &total_is_null;
    count_result[0].is_unsigned = 1;
    if (mysql_stmt_bind_result(count_stmt, count_result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    const int count_fetch_status = mysql_stmt_fetch(count_stmt);
    if (count_fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        return false;
    }
    mysql_stmt_close(count_stmt);

    unsigned long long offset = page > 0 ? static_cast<unsigned long long>((page - 1) * limit) : 0;
    unsigned long long limit_value = static_cast<unsigned long long>(limit);
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *sql =
        "SELECT images.id, images.filename, images.url, COALESCE(images.uploader, ''), images.size, "
        "images.like_count, images.comment_count, images.favorite_count, images.download_count, "
        "DATE_FORMAT(images.created_at, '%Y-%m-%d %H:%i:%s'), "
        "EXISTS(SELECT 1 FROM image_likes WHERE image_likes.image_id=images.id AND image_likes.username=?), "
        "DATE_FORMAT(image_favorites.created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM image_favorites JOIN images ON image_favorites.image_id=images.id "
        "WHERE image_favorites.username=? "
        "ORDER BY image_favorites.created_at DESC, images.id DESC LIMIT ? OFFSET ?";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[4];
    std::memset(param, 0, sizeof(param));
    unsigned long liked_username_length = 0;
    unsigned long favorites_username_length = 0;
    bind_string_param(param[0], username, liked_username_length);
    bind_string_param(param[1], username, favorites_username_length);
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &limit_value;
    param[2].buffer_length = sizeof(limit_value);
    param[2].is_unsigned = 1;
    param[3].buffer_type = MYSQL_TYPE_LONGLONG;
    param[3].buffer = &offset;
    param[3].buffer_length = sizeof(offset);
    param[3].is_unsigned = 1;
    if (mysql_stmt_bind_param(stmt, param) != 0 || mysql_stmt_execute(stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
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

    FavoriteImageRecord row;
    std::vector<char> filename_buffer(1024);
    std::vector<char> url_buffer(2048);
    std::vector<char> uploader_buffer(256);
    std::vector<char> created_buffer(32);
    std::vector<char> favorited_buffer(32);
    int liked_marker = 0;
    unsigned long lengths[12] = {0};
    MysqlBool nulls[12] = {0};
    MYSQL_BIND result[12];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &row.image.id;
    result[0].buffer_length = sizeof(row.image.id);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = filename_buffer.data();
    result[1].buffer_length = static_cast<unsigned long>(filename_buffer.size());
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = url_buffer.data();
    result[2].buffer_length = static_cast<unsigned long>(url_buffer.size());
    result[2].length = &lengths[2];
    result[2].is_null = &nulls[2];
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = uploader_buffer.data();
    result[3].buffer_length = static_cast<unsigned long>(uploader_buffer.size());
    result[3].length = &lengths[3];
    result[3].is_null = &nulls[3];
    result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result[4].buffer = &row.image.size;
    result[4].buffer_length = sizeof(row.image.size);
    result[4].length = &lengths[4];
    result[4].is_null = &nulls[4];
    result[4].is_unsigned = 1;
    result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    result[5].buffer = &row.image.like_count;
    result[5].buffer_length = sizeof(row.image.like_count);
    result[5].length = &lengths[5];
    result[5].is_null = &nulls[5];
    result[5].is_unsigned = 1;
    result[6].buffer_type = MYSQL_TYPE_LONGLONG;
    result[6].buffer = &row.image.comment_count;
    result[6].buffer_length = sizeof(row.image.comment_count);
    result[6].length = &lengths[6];
    result[6].is_null = &nulls[6];
    result[6].is_unsigned = 1;
    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &row.image.favorite_count;
    result[7].buffer_length = sizeof(row.image.favorite_count);
    result[7].length = &lengths[7];
    result[7].is_null = &nulls[7];
    result[7].is_unsigned = 1;
    result[8].buffer_type = MYSQL_TYPE_LONGLONG;
    result[8].buffer = &row.image.download_count;
    result[8].buffer_length = sizeof(row.image.download_count);
    result[8].length = &lengths[8];
    result[8].is_null = &nulls[8];
    result[8].is_unsigned = 1;
    result[9].buffer_type = MYSQL_TYPE_STRING;
    result[9].buffer = created_buffer.data();
    result[9].buffer_length = static_cast<unsigned long>(created_buffer.size());
    result[9].length = &lengths[9];
    result[9].is_null = &nulls[9];
    result[10].buffer_type = MYSQL_TYPE_LONG;
    result[10].buffer = &liked_marker;
    result[10].buffer_length = sizeof(liked_marker);
    result[10].length = &lengths[10];
    result[10].is_null = &nulls[10];
    result[11].buffer_type = MYSQL_TYPE_STRING;
    result[11].buffer = favorited_buffer.data();
    result[11].buffer_length = static_cast<unsigned long>(favorited_buffer.size());
    result[11].length = &lengths[11];
    result[11].is_null = &nulls[11];
    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    while (true)
    {
        std::memset(nulls, 0, sizeof(nulls));
        row = FavoriteImageRecord();
        liked_marker = 0;
        const int fetch_status = mysql_stmt_fetch(stmt);
        if (fetch_status == MYSQL_NO_DATA)
        {
            break;
        }
        if (fetch_status == 1)
        {
            std::ostringstream message;
            message << "mysql_stmt_fetch failed: " << mysql_stmt_error(stmt);
            set_error(message.str());
            mysql_stmt_close(stmt);
            return false;
        }
        row.image.filename.assign(filename_buffer.data(), lengths[1]);
        row.image.url.assign(url_buffer.data(), lengths[2]);
        row.image.uploader.assign(uploader_buffer.data(), lengths[3]);
        row.image.created_at.assign(created_buffer.data(), lengths[9]);
        row.image.liked = liked_marker != 0;
        row.image.favorited = true;
        row.favorited_at.assign(favorited_buffer.data(), lengths[11]);
        favorites_out.push_back(row);
    }

    mysql_stmt_close(stmt);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::soft_delete_image_comment(unsigned long long comment_id,
                                            const std::string &username,
                                            bool is_admin,
                                            unsigned long long &image_id_out)
{
    image_id_out = 0;
#if !CGMYSQL_HAS_CLIENT
    (void)comment_id;
    (void)username;
    (void)is_admin;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (comment_id == 0 || username.empty())
    {
        set_error("Invalid image comment request.");
        return false;
    }

    CGMysqlGuard guard(*this);
    if (!guard.valid())
    {
        set_error("No database connection is available.");
        return false;
    }

    MYSQL *conn = guard.get();
    if (mysql_query(conn, "START TRANSACTION") != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        return false;
    }

    MYSQL_STMT *select_stmt = mysql_stmt_init(conn);
    if (select_stmt == nullptr)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("mysql_stmt_init failed.");
        return false;
    }
    const char *select_sql = "SELECT image_id, username FROM image_comments WHERE id=? AND deleted_at IS NULL LIMIT 1";
    if (mysql_stmt_prepare(select_stmt, select_sql, static_cast<unsigned long>(std::strlen(select_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    MYSQL_BIND select_param[1];
    std::memset(select_param, 0, sizeof(select_param));
    select_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    select_param[0].buffer = &comment_id;
    select_param[0].buffer_length = sizeof(comment_id);
    select_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(select_stmt, select_param) != 0 || mysql_stmt_execute(select_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }

    std::vector<char> owner_buffer(256);
    unsigned long lengths[2] = {0};
    MysqlBool nulls[2] = {0};
    MYSQL_BIND result[2];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &image_id_out;
    result[0].buffer_length = sizeof(image_id_out);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = owner_buffer.data();
    result[1].buffer_length = static_cast<unsigned long>(owner_buffer.size());
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    if (mysql_stmt_bind_result(select_stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    const int fetch_status = mysql_stmt_fetch(select_stmt);
    if (fetch_status == MYSQL_NO_DATA)
    {
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        set_error("Comment was not found.");
        return false;
    }
    if (fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    const std::string owner(owner_buffer.data(), lengths[1]);
    mysql_stmt_close(select_stmt);

    if (!is_admin && owner != username)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("Comment delete is forbidden.");
        return false;
    }

    MYSQL_STMT *delete_stmt = mysql_stmt_init(conn);
    if (delete_stmt == nullptr)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("mysql_stmt_init failed.");
        return false;
    }
    const char *delete_sql = "UPDATE image_comments SET deleted_at=CURRENT_TIMESTAMP WHERE id=? AND deleted_at IS NULL LIMIT 1";
    if (mysql_stmt_prepare(delete_stmt, delete_sql, static_cast<unsigned long>(std::strlen(delete_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(delete_stmt);
        set_error(message.str());
        mysql_stmt_close(delete_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    MYSQL_BIND delete_param[1];
    std::memset(delete_param, 0, sizeof(delete_param));
    delete_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    delete_param[0].buffer = &comment_id;
    delete_param[0].buffer_length = sizeof(comment_id);
    delete_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(delete_stmt, delete_param) != 0 || mysql_stmt_execute(delete_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(delete_stmt);
        set_error(message.str());
        mysql_stmt_close(delete_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    const bool changed = mysql_stmt_affected_rows(delete_stmt) > 0;
    mysql_stmt_close(delete_stmt);
    if (!changed)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("Comment was not found.");
        return false;
    }

    MYSQL_STMT *count_stmt = mysql_stmt_init(conn);
    if (count_stmt == nullptr)
    {
        mysql_query(conn, "ROLLBACK");
        set_error("mysql_stmt_init failed.");
        return false;
    }
    const char *count_sql = "UPDATE images SET comment_count=IF(comment_count>0,comment_count-1,0) WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(count_stmt, count_sql, static_cast<unsigned long>(std::strlen(count_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    MYSQL_BIND count_param[1];
    std::memset(count_param, 0, sizeof(count_param));
    count_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    count_param[0].buffer = &image_id_out;
    count_param[0].buffer_length = sizeof(image_id_out);
    count_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(count_stmt, count_param) != 0 || mysql_stmt_execute(count_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(count_stmt);
        set_error(message.str());
        mysql_stmt_close(count_stmt);
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    mysql_stmt_close(count_stmt);

    if (mysql_query(conn, "COMMIT") != 0)
    {
        std::ostringstream message;
        message << "mysql_query failed: " << mysql_error(conn);
        set_error(message.str());
        mysql_query(conn, "ROLLBACK");
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::fetch_image_by_id(unsigned long long image_id, ImageRecord &image_out)
{
    image_out = ImageRecord();
#if !CGMYSQL_HAS_CLIENT
    (void)image_id;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (image_id == 0)
    {
        set_error("Invalid image id.");
        return false;
    }

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

    const char *sql =
        "SELECT id, filename, url, COALESCE(uploader, ''), size, "
        "like_count, comment_count, favorite_count, download_count, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM images WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[1];
    std::memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &image_id;
    param[0].buffer_length = sizeof(image_id);
    param[0].is_unsigned = 1;
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

    std::vector<char> filename_buffer(1024);
    std::vector<char> url_buffer(2048);
    std::vector<char> uploader_buffer(256);
    std::vector<char> created_buffer(32);
    unsigned long lengths[10] = {0};
    MysqlBool nulls[10] = {0};
    MYSQL_BIND result[10];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &image_out.id;
    result[0].buffer_length = sizeof(image_out.id);
    result[0].length = &lengths[0];
    result[0].is_null = &nulls[0];
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = filename_buffer.data();
    result[1].buffer_length = static_cast<unsigned long>(filename_buffer.size());
    result[1].length = &lengths[1];
    result[1].is_null = &nulls[1];
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = url_buffer.data();
    result[2].buffer_length = static_cast<unsigned long>(url_buffer.size());
    result[2].length = &lengths[2];
    result[2].is_null = &nulls[2];
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = uploader_buffer.data();
    result[3].buffer_length = static_cast<unsigned long>(uploader_buffer.size());
    result[3].length = &lengths[3];
    result[3].is_null = &nulls[3];
    result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result[4].buffer = &image_out.size;
    result[4].buffer_length = sizeof(image_out.size);
    result[4].length = &lengths[4];
    result[4].is_null = &nulls[4];
    result[4].is_unsigned = 1;
    result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    result[5].buffer = &image_out.like_count;
    result[5].buffer_length = sizeof(image_out.like_count);
    result[5].length = &lengths[5];
    result[5].is_null = &nulls[5];
    result[5].is_unsigned = 1;
    result[6].buffer_type = MYSQL_TYPE_LONGLONG;
    result[6].buffer = &image_out.comment_count;
    result[6].buffer_length = sizeof(image_out.comment_count);
    result[6].length = &lengths[6];
    result[6].is_null = &nulls[6];
    result[6].is_unsigned = 1;
    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &image_out.favorite_count;
    result[7].buffer_length = sizeof(image_out.favorite_count);
    result[7].length = &lengths[7];
    result[7].is_null = &nulls[7];
    result[7].is_unsigned = 1;
    result[8].buffer_type = MYSQL_TYPE_LONGLONG;
    result[8].buffer = &image_out.download_count;
    result[8].buffer_length = sizeof(image_out.download_count);
    result[8].length = &lengths[8];
    result[8].is_null = &nulls[8];
    result[8].is_unsigned = 1;
    result[9].buffer_type = MYSQL_TYPE_STRING;
    result[9].buffer = created_buffer.data();
    result[9].buffer_length = static_cast<unsigned long>(created_buffer.size());
    result[9].length = &lengths[9];
    result[9].is_null = &nulls[9];

    if (mysql_stmt_bind_result(stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    const int fetch_status = mysql_stmt_fetch(stmt);
    if (fetch_status == MYSQL_NO_DATA)
    {
        set_error("Image was not found.");
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

    image_out.filename.assign(filename_buffer.data(), lengths[1]);
    image_out.url.assign(url_buffer.data(), lengths[2]);
    image_out.uploader.assign(uploader_buffer.data(), lengths[3]);
    image_out.created_at.assign(created_buffer.data(), lengths[9]);
    mysql_stmt_close(stmt);
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_last_error.clear();
    }
    return true;
#endif
}

bool CGMysqlPool::increment_image_download_count(unsigned long long image_id, unsigned long long &download_count_out)
{
    download_count_out = 0;
#if !CGMYSQL_HAS_CLIENT
    (void)image_id;
    set_error("MySQL client support is unavailable in this build.");
    return false;
#else
    if (!m_images_table_supported)
    {
        set_error("Images table is unavailable.");
        return false;
    }
    if (image_id == 0)
    {
        set_error("Invalid image id.");
        return false;
    }

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

    const char *sql = "UPDATE images SET download_count=download_count+1 WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        set_error(message.str());
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND param[1];
    std::memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &image_id;
    param[0].buffer_length = sizeof(image_id);
    param[0].is_unsigned = 1;
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
    if (mysql_stmt_affected_rows(stmt) == 0)
    {
        set_error("Image was not found.");
        mysql_stmt_close(stmt);
        return false;
    }
    mysql_stmt_close(stmt);

    MYSQL_STMT *select_stmt = mysql_stmt_init(conn);
    if (select_stmt == nullptr)
    {
        set_error("mysql_stmt_init failed.");
        return false;
    }

    const char *select_sql = "SELECT download_count FROM images WHERE id=? LIMIT 1";
    if (mysql_stmt_prepare(select_stmt, select_sql, static_cast<unsigned long>(std::strlen(select_sql))) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_prepare failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }
    MYSQL_BIND select_param[1];
    std::memset(select_param, 0, sizeof(select_param));
    select_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    select_param[0].buffer = &image_id;
    select_param[0].buffer_length = sizeof(image_id);
    select_param[0].is_unsigned = 1;
    if (mysql_stmt_bind_param(select_stmt, select_param) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_param failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }
    if (mysql_stmt_execute(select_stmt) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_execute failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }

    unsigned long length = 0;
    MysqlBool is_null = 0;
    MYSQL_BIND result[1];
    std::memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &download_count_out;
    result[0].buffer_length = sizeof(download_count_out);
    result[0].length = &length;
    result[0].is_null = &is_null;
    result[0].is_unsigned = 1;
    if (mysql_stmt_bind_result(select_stmt, result) != 0)
    {
        std::ostringstream message;
        message << "mysql_stmt_bind_result failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }
    const int fetch_status = mysql_stmt_fetch(select_stmt);
    if (fetch_status == MYSQL_NO_DATA || is_null)
    {
        set_error("Image was not found.");
        mysql_stmt_close(select_stmt);
        return false;
    }
    if (fetch_status == 1)
    {
        std::ostringstream message;
        message << "mysql_stmt_fetch failed: " << mysql_stmt_error(select_stmt);
        set_error(message.str());
        mysql_stmt_close(select_stmt);
        return false;
    }
    mysql_stmt_close(select_stmt);
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
