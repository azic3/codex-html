#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <string>
#include <ctime>
#include <memory>
#include <sys/epoll.h>
#include <unordered_map>
#include <mutex>
#include <vector>

#include "CGmysql.h"
#include "app_logger.h"
#include "http_conn.h"
#include "password_hasher.h"
#include "redis_client.h"
#include "smtp_client.h"
#include "threadpool.h"

const int MAX_EVENT_NUMBER = 1024;
const int TIMESLOT = 5;

class UtilTimer
{
public:
    UtilTimer();

public:
    int sockfd;
    time_t expire;
    UtilTimer *prev;
    UtilTimer *next;
};

class SortTimerList
{
public:
    SortTimerList();
    ~SortTimerList();

    void add_timer(UtilTimer *timer);
    void adjust_timer(UtilTimer *timer);
    void del_timer(UtilTimer *timer);
    std::vector<int> tick();

private:
    void add_timer(UtilTimer *timer, UtilTimer *lst_head);

private:
    UtilTimer *m_head;
    UtilTimer *m_tail;
};

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port,
              const std::string &user,
              const std::string &passWord,
              const std::string &databaseName,
              const std::string &databaseHost,
              int databasePort,
              const std::string &staticRoot,
              std::size_t maxImageUploadSize,
              std::size_t maxVideoUploadSize,
              int log_write,
              int opt_linger,
              int trigmode,
              int sql_num,
              int thread_num,
              int close_log,
              int actor_model);

    void trig_mode();
    void eventListen();
    void eventLoop();
    bool dealclientdata();
    void dealwithread(int sockfd);
    void process_request(int sockfd, const std::string &request_text);
    void timer(int connfd);
    void adjust_timer(int sockfd);
    void deal_timer(int sockfd);
    void timer_handler();

private:
    bool addfd(int fd, bool one_shot);
    bool modfd(int fd, uint32_t events);
    void closefd(int fd);
    void remove_pending_request(int fd);
    HttpConn::Response handle_request(const HttpConn::Request &request) const;
    HttpConn::Response handle_login_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_register_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_send_email_code_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_reset_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_upload_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_upload_video_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_upload_video_chunk_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_list_images_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_my_favorites_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_image_reaction_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_list_image_comments_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_create_image_comment_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_delete_image_comment_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_image_download_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_list_videos_api() const;
    HttpConn::Response handle_media_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_current_user_api(const HttpConn::Request &request) const;
    HttpConn::Response handle_logout_api(const HttpConn::Request &request) const;
    HttpConn::Response build_error_response(int status_code, const std::string &status_text, const std::string &body) const;
    HttpConn::Response build_response_with_body(int status_code,
                                                const std::string &status_text,
                                                const std::string &content_type,
                                                const std::string &body) const;
    HttpConn::Response build_static_file_response(const HttpConn::Request &request, const std::string &path) const;
    std::string build_directory_listing(const std::string &request_path, const std::string &directory_path) const;
    std::string get_content_type(const std::string &path) const;
    bool is_safe_path(const std::string &path) const;
    std::string read_file(const std::string &path) const;
    std::string read_file_range(const std::string &path, unsigned long long start, unsigned long long length) const;
    bool is_directory(const std::string &path) const;
    bool file_exists(const std::string &path) const;
    std::string join_path(const std::string &base, const std::string &name) const;
    std::string html_escape(const std::string &value) const;
    std::string json_escape_string(const std::string &value) const;
    std::string url_encode_path_segment(const std::string &value) const;
    bool read_http_request(int sockfd, std::string &request_text);
    bool send_all(int sockfd, const std::string &data) const;
    std::string get_header_value(const HttpConn::Request &request, const std::string &name) const;
    std::string sanitize_upload_filename(const std::string &filename) const;
    std::string unique_upload_filename(const std::string &directory_name, const std::string &filename) const;
    bool ensure_directory_exists(const std::string &path) const;
    bool image_record_file_exists(const CGMysqlPool::ImageRecord &image) const;
    std::string build_images_json(const std::string &username) const;
    std::string build_images_page_json(std::size_t page, std::size_t limit, const std::string &username) const;
    std::string build_videos_json() const;
    void import_existing_images_to_db() const;
    std::string build_cached_images_json() const;
    std::string build_cached_videos_json() const;
    void invalidate_media_list_cache(const std::string &directory_name) const;
    bool save_uploaded_image(const HttpConn::Request &request, std::string &saved_path, std::string &detail) const;
    bool save_uploaded_media(const HttpConn::Request &request,
                             const std::string &directory_name,
                             const std::string &field_name,
                             bool expect_image,
                             std::string &saved_path,
                             std::string &detail) const;
    bool save_uploaded_video_chunk(const HttpConn::Request &request, std::string &saved_path, std::string &detail) const;
    bool validate_user_with_db(const std::string &username, const std::string &password, std::string &detail) const;
    bool register_user_with_db(const std::string &username, const std::string &password, std::string &detail) const;
    bool reset_user_password_with_db(const std::string &username, const std::string &password, std::string &detail) const;
    std::string create_session(const std::string &username, bool is_admin, bool remember) const;
    bool get_session(const HttpConn::Request &request, std::string &username, bool &is_admin) const;
    void destroy_session(const HttpConn::Request &request) const;
    bool current_user_is_admin(const HttpConn::Request &request) const;
    std::string get_client_ip(int sockfd) const;
    std::string generate_email_verification_code() const;
    std::string make_email_code_key(const std::string &phone, const std::string &email) const;
    void cleanup_expired_email_state(time_t now) const;
    bool consume_email_code_rate_limit(const std::string &phone, const std::string &ip, std::string &detail) const;
    bool save_email_verification_code(const std::string &phone, const std::string &email, const std::string &code, std::string &detail) const;
    bool verify_email_code(const std::string &phone, const std::string &email, const std::string &code, std::string &detail) const;

private:
    struct EmailVerificationCode
    {
        std::string phone;
        std::string code;
        time_t expire;
        int failed_attempts;
    };

    struct EmailCodeRateState
    {
        time_t last_request_at;
        time_t minute_window_start;
        int minute_count;
        time_t daily_window_start;
        int daily_count;
    };

    struct UserSession
    {
        std::string username;
        bool is_admin;
        time_t created_at;
        time_t expire_at;
    };

    int m_port;
    int m_log_write;
    int m_close_log;
    int m_actormodel;
    int m_listenfd;
    int m_epollfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;
    int m_sql_num;
    int m_thread_num;
    std::string m_root;
    std::string m_db_host;
    int m_db_port;
    std::string m_db_user;
    std::string m_db_password;
    std::string m_db_name;
    std::size_t m_max_image_upload_size;
    std::size_t m_max_video_upload_size;
    HttpConn m_http_conn;
    mutable CGMysqlPool m_db_pool;
    mutable RedisClient m_redis;
    std::unique_ptr<ThreadPool> m_thread_pool;
    std::unordered_map<int, std::string> m_pending_requests;
    std::unordered_map<int, UtilTimer *> m_conn_timers;
    mutable std::unordered_map<std::string, EmailVerificationCode> m_email_codes;
    mutable std::unordered_map<std::string, EmailCodeRateState> m_email_code_phone_limits;
    mutable std::unordered_map<std::string, EmailCodeRateState> m_email_code_ip_limits;
    mutable std::unordered_map<std::string, UserSession> m_sessions;
    mutable std::mutex m_pending_mutex;
    mutable std::mutex m_timer_mutex;
    mutable std::mutex m_email_code_mutex;
    mutable std::mutex m_session_mutex;
    int m_pipefd[2];
    SortTimerList m_timer_list;
    epoll_event m_events[MAX_EVENT_NUMBER];
};

#endif
