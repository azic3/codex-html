#ifndef CGMYSQL_H
#define CGMYSQL_H

#include <cstddef>
#include <queue>
#include <string>
#include <vector>

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
    struct ImageRecord
    {
        unsigned long long id;
        std::string filename;
        std::string url;
        std::string uploader;
        unsigned long long size;
        unsigned long long like_count;
        unsigned long long comment_count;
        unsigned long long favorite_count;
        unsigned long long download_count;
        bool liked;
        bool favorited;
        std::string created_at;
    };

    struct ImageReactionState
    {
        unsigned long long image_id;
        unsigned long long like_count;
        unsigned long long favorite_count;
        bool liked;
        bool favorited;
    };

    struct ImageCommentRecord
    {
        unsigned long long id;
        unsigned long long image_id;
        std::string username;
        std::string content;
        std::string created_at;
    };

    struct FavoriteImageRecord
    {
        ImageRecord image;
        std::string favorited_at;
    };

    struct UserProfileRecord
    {
        std::string username;
        std::string avatar_url;
        std::string bio;
        std::string created_at;
        std::string updated_at;
    };

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
    bool fetch_user_credentials(const std::string &username,
                                std::string &password_out,
                                std::string &password_version_out);
    bool user_exists(const std::string &username, bool &exists_out);
    bool insert_user(const std::string &username, const std::string &password);
    bool insert_user(const std::string &username, const std::string &password, const std::string &password_version);
    bool update_user_password(const std::string &username, const std::string &password);
    bool update_user_password(const std::string &username, const std::string &password, const std::string &password_version);
    bool fetch_user_profile(const std::string &username, UserProfileRecord &profile_out);
    bool upsert_user_profile(const std::string &username,
                             const std::string &avatar_url,
                             const std::string &bio);
    bool insert_image(const std::string &filename,
                      const std::string &url,
                      const std::string &uploader,
                      unsigned long long size,
                      unsigned long long &id_out);
    bool fetch_images_page(std::size_t page,
                           std::size_t limit,
                           const std::string &username,
                           std::vector<ImageRecord> &images_out,
                           unsigned long long &total_out);
    bool set_image_like(unsigned long long image_id,
                        const std::string &username,
                        bool active,
                        ImageReactionState &state_out);
    bool set_image_favorite(unsigned long long image_id,
                            const std::string &username,
                            bool active,
                            ImageReactionState &state_out);
    bool insert_image_comment(unsigned long long image_id,
                              const std::string &username,
                              const std::string &content,
                              ImageCommentRecord &comment_out);
    bool fetch_image_comments(unsigned long long image_id,
                              std::size_t page,
                              std::size_t limit,
                              std::vector<ImageCommentRecord> &comments_out,
                              unsigned long long &total_out);
    bool fetch_user_favorites(const std::string &username,
                              std::size_t page,
                              std::size_t limit,
                              std::vector<FavoriteImageRecord> &favorites_out,
                              unsigned long long &total_out);
    bool soft_delete_image_comment(unsigned long long comment_id,
                                   const std::string &username,
                                   bool is_admin,
                                   unsigned long long &image_id_out);
    bool fetch_image_by_id(unsigned long long image_id, ImageRecord &image_out);
    bool increment_image_download_count(unsigned long long image_id, unsigned long long &download_count_out);

private:
    void set_error(const std::string &message);
    bool set_image_reaction(unsigned long long image_id,
                            const std::string &username,
                            bool active,
                            bool favorite,
                            ImageReactionState &state_out);

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
    bool m_password_version_supported;
    bool m_images_table_supported;
    bool m_user_profiles_table_supported;
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
