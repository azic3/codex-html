#ifndef CGMYSQL_H
#define CGMYSQL_H

#include <queue>
#include <string>

#include <condition_variable>
#include <mutex>

#if defined(__has_include)
#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#define CGMYSQL_HAS_CLIENT 1
#elif __has_include(<mysql.h>)
#include <mysql.h>
#define CGMYSQL_HAS_CLIENT 1
#else
#define CGMYSQL_HAS_CLIENT 0
typedef struct st_mysql MYSQL;
typedef struct st_mysql_res MYSQL_RES;
typedef char **MYSQL_ROW;
#endif
#else
#define CGMYSQL_HAS_CLIENT 0
typedef struct st_mysql MYSQL;
typedef struct st_mysql_res MYSQL_RES;
typedef char **MYSQL_ROW;
#endif

class CGMysqlPool
{
public:
    CGMysqlPool();
    ~CGMysqlPool();

    bool init(const std::string &host,
              unsigned int port,
              const std::string &user,
              const std::string &password,
              const std::string &database,
              int max_conn);

    MYSQL *get_connection();
    void release_connection(MYSQL *conn);

    bool available() const;
    std::string last_error() const;
    bool fetch_user_password(const std::string &username, std::string &password_out);
    bool user_exists(const std::string &username, bool &exists_out);
    bool insert_user(const std::string &username, const std::string &password);
    bool update_user_password(const std::string &username, const std::string &password);

private:
    void set_error(const std::string &message);
    std::string escape_string(MYSQL *conn, const std::string &value);

private:
    std::queue<MYSQL *> m_connections;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::string m_last_error;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_database;
    unsigned int m_port;
    int m_max_conn;
    bool m_initialized;
};

class CGMysqlGuard
{
public:
    explicit CGMysqlGuard(CGMysqlPool &pool);
    ~CGMysqlGuard();

    MYSQL *get() const;
    bool valid() const;

private:
    CGMysqlPool &m_pool;
    MYSQL *m_conn;
};

#endif
