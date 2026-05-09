#include "webserver.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
const std::size_t kReadBufferSize = 4096;
const int kConnectionTimeout = 3 * TIMESLOT;
const int kEmailCodeTimeout = 5 * 60;
const std::size_t kMaxImageUploadSize = 20 * 1024 * 1024;
const std::size_t kMaxVideoUploadSize = 1024ULL * 1024 * 1024;
int g_signal_pipefd[2] = {-1, -1};

int set_nonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

std::size_t parse_content_length(const std::string &request_text)
{
    const std::string key = "Content-Length:";
    const std::size_t pos = request_text.find(key);
    if (pos == std::string::npos)
    {
        return 0;
    }

    std::size_t value_start = pos + key.size();
    while (value_start < request_text.size() &&
           (request_text[value_start] == ' ' || request_text[value_start] == '\t'))
    {
        ++value_start;
    }

    const std::size_t value_end = request_text.find("\r\n", value_start);
    const std::string value = request_text.substr(value_start, value_end - value_start);
    return static_cast<std::size_t>(std::atoi(value.c_str()));
}

std::string lower_ext(const std::string &path)
{
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
    {
        return std::string();
    }

    std::string ext = path.substr(dot + 1);
    for (std::size_t i = 0; i < ext.size(); ++i)
    {
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    }
    return ext;
}

bool is_image_extension(const std::string &path)
{
    const std::string ext = lower_ext(path);
    return ext == "svg" || ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "gif" || ext == "bmp" || ext == "webp" || ext == "ico" ||
           ext == "avif";
}

bool is_safe_upload_extension(const std::string &path)
{
    const std::string ext = lower_ext(path);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
           ext == "bmp" || ext == "webp" || ext == "svg";
}

bool is_video_extension(const std::string &path)
{
    const std::string ext = lower_ext(path);
    return ext == "mp4" || ext == "webm" || ext == "ogg" || ext == "mov" ||
           ext == "avi" || ext == "mkv" || ext == "m4v";
}

bool is_safe_video_upload_extension(const std::string &path)
{
    return is_video_extension(path);
}

bool is_valid_phone(const std::string &phone)
{
    if (phone.size() != 11 || phone[0] != '1')
    {
        return false;
    }

    for (std::size_t i = 0; i < phone.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(phone[i])))
        {
            return false;
        }
    }
    return true;
}

bool is_valid_email(const std::string &email)
{
    const std::size_t at = email.find('@');
    const std::size_t dot = email.rfind('.');
    if (email.find('@', at == std::string::npos ? 0 : at + 1) != std::string::npos)
    {
        return false;
    }

    for (std::size_t i = 0; i < email.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(email[i]);
        if (ch <= 32 || ch >= 127 || email[i] == '<' || email[i] == '>')
        {
            return false;
        }
    }

    return at != std::string::npos &&
           dot != std::string::npos &&
           at > 0 &&
           dot > at + 1 &&
           dot + 1 < email.size();
}

bool is_six_digit_code(const std::string &code)
{
    if (code.size() != 6)
    {
        return false;
    }

    for (std::size_t i = 0; i < code.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(code[i])))
        {
            return false;
        }
    }
    return true;
}

std::string json_escape(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        switch (value[i])
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(value[i]);
            break;
        }
    }
    return escaped;
}

bool request_is_complete(const std::string &request_text)
{
    const std::size_t header_end = request_text.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        return false;
    }

    const std::size_t expected_total = header_end + 4 + parse_content_length(request_text);
    return request_text.size() >= expected_total;
}

void sig_handler(int sig)
{
    const int saved_errno = errno;
    const int msg = sig;
    write(g_signal_pipefd[1], &msg, sizeof(msg));
    errno = saved_errno;
}

void addsig(int sig)
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}
}

UtilTimer::UtilTimer()
    : sockfd(-1),
      expire(0),
      prev(nullptr),
      next(nullptr)
{
}

SortTimerList::SortTimerList()
    : m_head(nullptr),
      m_tail(nullptr)
{
}

SortTimerList::~SortTimerList()
{
    UtilTimer *tmp = m_head;
    while (tmp != nullptr)
    {
        UtilTimer *next = tmp->next;
        delete tmp;
        tmp = next;
    }
}

void SortTimerList::add_timer(UtilTimer *timer)
{
    if (timer == nullptr)
    {
        return;
    }

    if (m_head == nullptr)
    {
        m_head = timer;
        m_tail = timer;
        return;
    }

    if (timer->expire < m_head->expire)
    {
        timer->next = m_head;
        m_head->prev = timer;
        m_head = timer;
        return;
    }

    add_timer(timer, m_head);
}

void SortTimerList::add_timer(UtilTimer *timer, UtilTimer *lst_head)
{
    UtilTimer *prev = lst_head;
    UtilTimer *tmp = prev->next;
    while (tmp != nullptr)
    {
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            return;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    prev->next = timer;
    timer->prev = prev;
    m_tail = timer;
}

void SortTimerList::adjust_timer(UtilTimer *timer)
{
    if (timer == nullptr)
    {
        return;
    }

    UtilTimer *tmp = timer->next;
    if (tmp == nullptr || timer->expire < tmp->expire)
    {
        return;
    }

    if (timer == m_head)
    {
        m_head = m_head->next;
        m_head->prev = nullptr;
        timer->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, m_head);
        return;
    }

    UtilTimer *next_node = timer->next;
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    if (timer == m_tail)
    {
        m_tail = timer->prev;
    }
    timer->prev = nullptr;
    timer->next = nullptr;
    add_timer(timer, next_node);
}

void SortTimerList::del_timer(UtilTimer *timer)
{
    if (timer == nullptr)
    {
        return;
    }

    if ((timer == m_head) && (timer == m_tail))
    {
        delete timer;
        m_head = nullptr;
        m_tail = nullptr;
        return;
    }

    if (timer == m_head)
    {
        m_head = m_head->next;
        m_head->prev = nullptr;
        delete timer;
        return;
    }

    if (timer == m_tail)
    {
        m_tail = m_tail->prev;
        m_tail->next = nullptr;
        delete timer;
        return;
    }

    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

std::vector<int> SortTimerList::tick()
{
    std::vector<int> expired_fds;
    if (m_head == nullptr)
    {
        return expired_fds;
    }

    const time_t cur = time(nullptr);
    UtilTimer *tmp = m_head;
    while (tmp != nullptr)
    {
        if (cur < tmp->expire)
        {
            break;
        }

        UtilTimer *next = tmp->next;
        expired_fds.push_back(tmp->sockfd);
        if (m_head == tmp)
        {
            m_head = next;
        }
        if (next != nullptr)
        {
            next->prev = nullptr;
        }
        else
        {
            m_tail = nullptr;
        }
        tmp->prev = nullptr;
        tmp->next = nullptr;
        delete tmp;
        tmp = next;
    }

    return expired_fds;
}

WebServer::WebServer()
    : m_port(9006),
      m_log_write(0),
      m_close_log(0),
      m_actormodel(0),
      m_listenfd(-1),
      m_epollfd(-1),
      m_OPT_LINGER(0),
      m_TRIGMode(0),
      m_LISTENTrigmode(0),
      m_CONNTrigmode(0),
      m_sql_num(0),
      m_thread_num(4),
      m_root("."),
      m_db_host("127.0.0.1"),
      m_db_user("root"),
      m_db_password("password"),
      m_db_name("qgydb")
{
    std::srand(static_cast<unsigned int>(std::time(nullptr) ^ getpid()));
    m_pipefd[0] = -1;
    m_pipefd[1] = -1;
}

WebServer::~WebServer()
{
    if (m_epollfd != -1)
    {
        close(m_epollfd);
    }
    if (m_listenfd != -1)
    {
        close(m_listenfd);
    }
    if (m_pipefd[0] != -1)
    {
        close(m_pipefd[0]);
    }
    if (m_pipefd[1] != -1)
    {
        close(m_pipefd[1]);
    }
}

void WebServer::init(int port,
                     const std::string &user,
                     const std::string &passWord,
                     const std::string &databaseName,
                     int log_write,
                     int opt_linger,
                     int trigmode,
                     int sql_num,
                     int thread_num,
                     int close_log,
                     int actor_model)
{
    m_port = port;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
    m_sql_num = sql_num > 0 ? sql_num : 4;
    m_thread_num = thread_num > 0 ? thread_num : 8;
    m_db_user = user;
    m_db_password = passWord;
    m_db_name = databaseName;

    trig_mode();
    m_db_pool.init(m_db_host, 3306, m_db_user, m_db_password, m_db_name, m_sql_num);
    m_thread_pool.reset(new ThreadPool(static_cast<std::size_t>(m_thread_num)));
}

void WebServer::trig_mode()
{
    switch (m_TRIGMode)
    {
    case 0:
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
        break;
    case 1:
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
        break;
    case 2:
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
        break;
    case 3:
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
        break;
    default:
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
        break;
    }
}

bool WebServer::addfd(int fd, bool one_shot)
{
    epoll_event event;
    std::memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (m_CONNTrigmode == 1 || (fd == m_listenfd && m_LISTENTrigmode == 1))
    {
        event.events |= EPOLLET;
    }
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    if (epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        return false;
    }

    set_nonblocking(fd);
    return true;
}

bool WebServer::modfd(int fd, uint32_t events)
{
    epoll_event event;
    std::memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = events | EPOLLRDHUP;
    if (m_CONNTrigmode == 1)
    {
        event.events |= EPOLLET;
    }
    event.events |= EPOLLONESHOT;
    return epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &event) != -1;
}

void WebServer::closefd(int fd)
{
    if (fd >= 0)
    {
        bool active_connection = (fd == m_listenfd || fd == m_pipefd[0] || fd == m_pipefd[1]);
        {
            std::unique_lock<std::mutex> lock(m_timer_mutex);
            std::unordered_map<int, UtilTimer *>::iterator it = m_conn_timers.find(fd);
            if (it != m_conn_timers.end())
            {
                m_timer_list.del_timer(it->second);
                m_conn_timers.erase(it);
                active_connection = true;
            }
        }
        if (!active_connection)
        {
            return;
        }
        remove_pending_request(fd);
        epoll_ctl(m_epollfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
    }
}

void WebServer::remove_pending_request(int fd)
{
    std::unique_lock<std::mutex> lock(m_pending_mutex);
    m_pending_requests.erase(fd);
}

void WebServer::eventListen()
{
    std::signal(SIGPIPE, SIG_IGN);
    addsig(SIGALRM);

    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0)
    {
        throw std::runtime_error("socket creation failed");
    }

    if (m_OPT_LINGER == 0)
    {
        linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (m_OPT_LINGER == 1)
    {
        linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    if (bind(m_listenfd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        throw std::runtime_error("bind failed");
    }

    if (listen(m_listenfd, SOMAXCONN) < 0)
    {
        throw std::runtime_error("listen failed");
    }

    m_epollfd = epoll_create(8);
    if (m_epollfd < 0)
    {
        throw std::runtime_error("epoll_create failed");
    }

    if (!addfd(m_listenfd, false))
    {
        throw std::runtime_error("failed to add listen fd to epoll");
    }

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd) == -1)
    {
        throw std::runtime_error("socketpair failed");
    }
    set_nonblocking(m_pipefd[1]);
    set_nonblocking(m_pipefd[0]);
    g_signal_pipefd[0] = m_pipefd[0];
    g_signal_pipefd[1] = m_pipefd[1];
    if (!addfd(m_pipefd[0], false))
    {
        throw std::runtime_error("failed to add signal pipe fd to epoll");
    }

    alarm(TIMESLOT);

    char cwd[512] = {0};
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
    {
        m_root = std::string(cwd) + "/public";
    }

    std::cout << "Server listening on 0.0.0.0:" << m_port << std::endl;
    if (!m_db_pool.available())
    {
        std::cout << "MySQL pool unavailable: " << m_db_pool.last_error() << std::endl;
    }
}

bool WebServer::dealclientdata()
{
    sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    if (m_LISTENTrigmode == 0)
    {
        int connfd = accept(m_listenfd, reinterpret_cast<sockaddr *>(&client_address), &client_addrlength);
        if (connfd < 0)
        {
            return false;
        }
        if (!addfd(connfd, true))
        {
            close(connfd);
            return false;
        }
        timer(connfd);
        return true;
    }

    while (true)
    {
        int connfd = accept(m_listenfd, reinterpret_cast<sockaddr *>(&client_address), &client_addrlength);
        if (connfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        if (!addfd(connfd, true))
        {
            close(connfd);
            continue;
        }
        timer(connfd);
    }

    return true;
}

void WebServer::timer(int connfd)
{
    UtilTimer *timer_node = new UtilTimer();
    timer_node->sockfd = connfd;
    timer_node->expire = time(nullptr) + kConnectionTimeout;

    std::unique_lock<std::mutex> lock(m_timer_mutex);
    m_conn_timers[connfd] = timer_node;
    m_timer_list.add_timer(timer_node);
}

void WebServer::adjust_timer(int sockfd)
{
    std::unique_lock<std::mutex> lock(m_timer_mutex);
    std::unordered_map<int, UtilTimer *>::iterator it = m_conn_timers.find(sockfd);
    if (it == m_conn_timers.end())
    {
        return;
    }

    it->second->expire = time(nullptr) + kConnectionTimeout;
    m_timer_list.adjust_timer(it->second);
}

void WebServer::deal_timer(int sockfd)
{
    closefd(sockfd);
}

void WebServer::timer_handler()
{
    std::vector<int> expired_fds;
    {
        std::unique_lock<std::mutex> lock(m_timer_mutex);
        expired_fds = m_timer_list.tick();
        for (std::size_t i = 0; i < expired_fds.size(); ++i)
        {
            m_conn_timers.erase(expired_fds[i]);
        }
    }

    for (std::size_t i = 0; i < expired_fds.size(); ++i)
    {
        epoll_ctl(m_epollfd, EPOLL_CTL_DEL, expired_fds[i], nullptr);
        remove_pending_request(expired_fds[i]);
        close(expired_fds[i]);
    }

    alarm(TIMESLOT);
}

std::string WebServer::read_file(const std::string &path) const
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open())
    {
        return std::string();
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WebServer::file_exists(const std::string &path) const
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool WebServer::is_directory(const std::string &path) const
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string WebServer::join_path(const std::string &base, const std::string &name) const
{
    if (base.empty() || base[base.size() - 1] == '/')
    {
        return base + name;
    }
    return base + "/" + name;
}

bool WebServer::is_safe_path(const std::string &path) const
{
    return path.find("..") == std::string::npos;
}

std::string WebServer::html_escape(const std::string &value) const
{
    std::string escaped;
    escaped.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        switch (value[i])
        {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(value[i]);
            break;
        }
    }

    return escaped;
}

std::string WebServer::json_escape_string(const std::string &value) const
{
    return json_escape(value);
}

std::string WebServer::build_directory_listing(const std::string &request_path, const std::string &directory_path) const
{
    DIR *dir = opendir(directory_path.c_str());
    if (dir == nullptr)
    {
        return "<html><body><h1>Unable to open directory</h1></body></html>";
    }

    std::ostringstream body;
    body << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
         << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
         << "<title>Directory Debug View</title>"
         << "<style>"
         << "body{margin:0;font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(180deg,#f5efe6,#f8fbff);color:#1f2937;}"
         << ".wrap{max-width:1080px;margin:0 auto;padding:32px 20px 48px;}"
         << ".hero{background:linear-gradient(135deg,#16324f,#27548a 55%,#6aa3d5);color:#fff;border-radius:24px;padding:28px 28px 24px;box-shadow:0 18px 45px rgba(22,50,79,.18);}"
         << ".eyebrow{font-size:12px;letter-spacing:.12em;text-transform:uppercase;opacity:.72;}"
         << "h1{margin:10px 0 8px;font-size:32px;}"
         << ".sub{margin:0;font-size:15px;line-height:1.7;opacity:.9;}"
         << ".toolbar{display:flex;flex-wrap:wrap;gap:12px;margin-top:18px;}"
         << ".pill{display:inline-flex;align-items:center;gap:8px;padding:10px 14px;border-radius:999px;background:rgba(255,255,255,.14);border:1px solid rgba(255,255,255,.18);font-size:13px;}"
         << ".grid{display:grid;grid-template-columns:2fr 1fr;gap:18px;margin-top:22px;}"
         << ".panel{background:#fff;border:1px solid #dbe5f0;border-radius:20px;padding:18px 18px 12px;box-shadow:0 14px 35px rgba(15,23,42,.06);}"
         << ".panel h2{margin:0 0 14px;font-size:18px;color:#10243b;}"
         << ".entries{display:grid;gap:12px;}"
         << ".image-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;}"
         << ".image-card{display:block;text-decoration:none;color:#10243b;border-radius:18px;overflow:hidden;background:#fff;border:1px solid #dbe5f0;box-shadow:0 12px 24px rgba(15,23,42,.06);}"
         << ".image-card:hover{transform:translateY(-2px);box-shadow:0 18px 30px rgba(15,23,42,.10);}"
         << ".thumb{height:160px;background:linear-gradient(135deg,#eef6ff,#f8fbff);display:flex;align-items:center;justify-content:center;padding:10px;}"
         << ".thumb img{max-width:100%;max-height:100%;display:block;border-radius:12px;}"
         << ".thumb-note{font-size:12px;color:#64748b;text-align:center;line-height:1.6;}"
         << ".image-meta{padding:12px 14px;}"
         << ".image-meta strong{display:block;font-size:14px;word-break:break-all;}"
         << ".image-meta span{display:block;font-size:12px;color:#64748b;margin-top:4px;word-break:break-all;}"
         << ".entry{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:14px 16px;border-radius:16px;background:#f8fbff;border:1px solid #dde9f4;}"
         << ".entry:hover{background:#eef6ff;border-color:#bfd7eb;}"
         << ".entry-main{min-width:0;}"
         << ".entry a{text-decoration:none;color:#133b63;font-weight:600;word-break:break-all;}"
         << ".meta{font-size:12px;color:#64748b;margin-top:4px;}"
         << ".badge{padding:7px 10px;border-radius:999px;font-size:12px;font-weight:700;white-space:nowrap;}"
         << ".dir{background:#e0f2fe;color:#075985;}"
         << ".file{background:#ede9fe;color:#5b21b6;}"
         << ".links{display:grid;gap:10px;}"
         << ".quick{display:block;padding:14px 16px;border-radius:16px;background:#f9fafb;border:1px solid #e5e7eb;text-decoration:none;color:#111827;}"
         << ".quick strong{display:block;font-size:14px;margin-bottom:4px;}"
         << ".quick span{font-size:12px;color:#6b7280;line-height:1.5;}"
         << "@media (max-width:860px){.grid{grid-template-columns:1fr;}h1{font-size:28px;}}"
         << "</style></head><body><div class=\"wrap\">"
         << "<section class=\"hero\">"
         << "<div class=\"eyebrow\">Epoll Static Server</div>"
         << "<h1>Directory Debug View</h1>"
         << "<p class=\"sub\">Current path: <strong>" << html_escape(request_path)
         << "</strong>. Use this page to debug static assets and inspect files quickly.</p>"
         << "<div class=\"toolbar\">"
         << "<div class=\"pill\">Root: " << html_escape(m_root) << "</div>"
         << "<div class=\"pill\">Port: " << m_port << "</div>"
         << "<div class=\"pill\">Mode: epoll</div>"
         << "</div></section>"
         << "<section class=\"grid\"><div class=\"panel\"><h2>Entries</h2>";

    const bool image_directory = request_path == "/images/" || request_path == "/images";
    body << (image_directory ? "<div class=\"image-grid\">" : "<div class=\"entries\">");

    if (request_path != "/")
    {
        std::string parent = request_path;
        while (!parent.empty() && parent[parent.size() - 1] == '/')
        {
            parent.erase(parent.size() - 1);
        }
        const std::size_t pos = parent.find_last_of('/');
        parent = (pos == std::string::npos || pos == 0) ? "/" : parent.substr(0, pos + 1);

        if (image_directory)
        {
            body << "<a class=\"image-card\" href=\"" << html_escape(parent) << "\">"
                 << "<div class=\"thumb\"><div class=\"thumb-note\">Back to parent</div></div>"
                 << "<div class=\"image-meta\"><strong>..</strong><span>" << html_escape(parent) << "</span></div></a>";
        }
        else
        {
            body << "<div class=\"entry\"><div class=\"entry-main\">"
                 << "<a href=\"" << html_escape(parent) << "\">..</a>"
                 << "<div class=\"meta\">Back to parent directory</div></div>"
                 << "<span class=\"badge dir\">PARENT</span></div>";
        }
    }

    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr)
    {
        const std::string name = entry->d_name;
        if (name == ".")
        {
            continue;
        }

        const std::string full_path = join_path(directory_path, name);
        const bool directory = is_directory(full_path);
        if (directory && (name == "css" || name == "js"))
        {
            continue;
        }
        std::string href = request_path;
        if (href.empty() || href[href.size() - 1] != '/')
        {
            href += "/";
        }
        href += name;
        if (directory)
        {
            href += "/";
        }

        if (image_directory && (directory || is_image_extension(name)))
        {
            body << "<a class=\"image-card\" href=\"" << html_escape(href) << "\"><div class=\"thumb\">";
            if (!directory)
            {
                body << "<img src=\"" << html_escape(href) << "\" alt=\"" << html_escape(name) << "\">";
            }
            else
            {
                body << "<div class=\"thumb-note\">Directory<br>Click to enter</div>";
            }
            body << "</div><div class=\"image-meta\"><strong>" << html_escape(name);
            if (directory)
            {
                body << "/";
            }
            body << "</strong><span>" << html_escape(full_path) << "</span></div></a>";
        }
        else
        {
            body << "<div class=\"entry\"><div class=\"entry-main\"><a href=\"" << html_escape(href) << "\">"
                 << html_escape(name);
            if (directory)
            {
                body << "/";
            }
            body << "</a><div class=\"meta\">" << html_escape(full_path) << "</div></div>"
                 << "<span class=\"badge " << (directory ? "dir" : "file") << "\">"
                 << (directory ? "DIR" : "FILE") << "</span></div>";
        }
    }

    closedir(dir);
    body << "</div></div><aside class=\"panel\"><h2>Quick Links</h2><div class=\"links\">"
         << "<a class=\"quick\" href=\"/login.html\"><strong>/login.html</strong><span>Login page entry.</span></a>"
         << "<a class=\"quick\" href=\"/app.html\"><strong>/app.html</strong><span>Application home after login.</span></a>"
         << "<a class=\"quick\" href=\"/images/\"><strong>/images/</strong><span>Image directory with previews.</span></a>"
         << "<a class=\"quick\" href=\"/video.html\"><strong>/video.html</strong><span>Video upload center.</span></a>"
         << "<a class=\"quick\" href=\"/videos/\"><strong>/videos/</strong><span>Uploaded video directory.</span></a>"
         << "</div></aside></section></div></body></html>";
    return body.str();
}

std::string WebServer::get_content_type(const std::string &path) const
{
    const std::string ext = lower_ext(path);
    if (ext == "html" || ext == "htm")
    {
        return "text/html; charset=utf-8";
    }
    if (ext == "css")
    {
        return "text/css; charset=utf-8";
    }
    if (ext == "js")
    {
        return "application/javascript; charset=utf-8";
    }
    if (ext == "json")
    {
        return "application/json; charset=utf-8";
    }
    if (ext == "txt")
    {
        return "text/plain; charset=utf-8";
    }
    if (ext == "svg")
    {
        return "image/svg+xml";
    }
    if (ext == "png")
    {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }
    if (ext == "gif")
    {
        return "image/gif";
    }
    if (ext == "webp")
    {
        return "image/webp";
    }
    if (ext == "bmp")
    {
        return "image/bmp";
    }
    if (ext == "avif")
    {
        return "image/avif";
    }
    if (ext == "ico")
    {
        return "image/x-icon";
    }
    if (ext == "mp4" || ext == "m4v")
    {
        return "video/mp4";
    }
    if (ext == "webm")
    {
        return "video/webm";
    }
    if (ext == "ogg")
    {
        return "video/ogg";
    }
    if (ext == "mov")
    {
        return "video/quicktime";
    }
    if (ext == "avi")
    {
        return "video/x-msvideo";
    }
    if (ext == "mkv")
    {
        return "video/x-matroska";
    }
    return "application/octet-stream";
}

HttpConn::Response WebServer::build_response_with_body(int status_code,
                                                       const std::string &status_text,
                                                       const std::string &content_type,
                                                       const std::string &body) const
{
    HttpConn::Response response;
    response.status_code = status_code;
    response.status_text = status_text;
    response.content_type = content_type;
    response.body = body;
    return response;
}

HttpConn::Response WebServer::build_error_response(int status_code,
                                                   const std::string &status_text,
                                                   const std::string &body) const
{
    return build_response_with_body(status_code, status_text, "text/html; charset=utf-8", body);
}

bool WebServer::validate_user_with_db(const std::string &username,
                                      const std::string &password,
                                      std::string &detail) const
{
    if (username.empty() || password.empty())
    {
        detail = "empty credentials";
        return false;
    }

    if (!m_db_pool.available())
    {
        detail = m_db_pool.last_error();
        return false;
    }

    std::string db_password;
    if (!m_db_pool.fetch_user_password(username, db_password))
    {
        detail = m_db_pool.last_error();
        return false;
    }

    bool matched = false;
    bool needs_rehash = false;
    if (!PasswordHasher::verify_password(password, db_password, matched, needs_rehash, detail))
    {
        return false;
    }

    if (!matched)
    {
        detail = "password mismatch";
        return false;
    }

    if (needs_rehash)
    {
        std::string hashed_password;
        std::string hash_detail;
        if (PasswordHasher::hash_password(password, hashed_password, hash_detail))
        {
            m_db_pool.update_user_password(username, hashed_password);
        }
    }

    detail.clear();
    return true;
}

bool WebServer::register_user_with_db(const std::string &username,
                                      const std::string &password,
                                      std::string &detail) const
{
    if (username.empty() || password.empty())
    {
        detail = "username or password is empty";
        return false;
    }

    if (!m_db_pool.available())
    {
        detail = m_db_pool.last_error();
        return false;
    }

    bool exists = false;
    if (!m_db_pool.user_exists(username, exists))
    {
        detail = m_db_pool.last_error();
        return false;
    }

    if (exists)
    {
        detail = "username already exists";
        return false;
    }

    std::string hashed_password;
    if (!PasswordHasher::hash_password(password, hashed_password, detail))
    {
        return false;
    }

    if (!m_db_pool.insert_user(username, hashed_password))
    {
        detail = m_db_pool.last_error();
        return false;
    }

    detail.clear();
    return true;
}

std::string WebServer::generate_email_verification_code() const
{
    const int value = 100000 + std::rand() % 900000;
    std::ostringstream code_stream;
    code_stream << value;
    return code_stream.str();
}

void WebServer::save_email_verification_code(const std::string &email, const std::string &code) const
{
    EmailVerificationCode record;
    record.code = code;
    record.expire = time(nullptr) + kEmailCodeTimeout;

    std::unique_lock<std::mutex> lock(m_email_code_mutex);
    m_email_codes[email] = record;
}

bool WebServer::verify_email_code(const std::string &email, const std::string &code, std::string &detail) const
{
    if (!is_valid_email(email))
    {
        detail = "email is invalid";
        return false;
    }

    if (!is_six_digit_code(code))
    {
        detail = "verification code is invalid";
        return false;
    }

    std::unique_lock<std::mutex> lock(m_email_code_mutex);
    std::unordered_map<std::string, EmailVerificationCode>::iterator it = m_email_codes.find(email);
    if (it == m_email_codes.end())
    {
        detail = "verification code was not sent";
        return false;
    }

    if (it->second.expire < time(nullptr))
    {
        m_email_codes.erase(it);
        detail = "verification code expired";
        return false;
    }

    if (it->second.code != code)
    {
        detail = "verification code mismatch";
        return false;
    }

    m_email_codes.erase(it);
    detail.clear();
    return true;
}

std::string WebServer::get_header_value(const HttpConn::Request &request, const std::string &name) const
{
    for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
         it != request.headers.end();
         ++it)
    {
        std::string key = it->first;
        for (std::size_t i = 0; i < key.size(); ++i)
        {
            key[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(key[i])));
        }

        std::string expected = name;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            expected[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(expected[i])));
        }

        if (key == expected)
        {
            return it->second;
        }
    }

    return std::string();
}

std::string WebServer::sanitize_upload_filename(const std::string &filename) const
{
    std::string base = filename;
    const std::size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        base = base.substr(slash + 1);
    }

    std::string cleaned;
    cleaned.reserve(base.size());
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(base[i]);
        if (std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-')
        {
            cleaned.push_back(base[i]);
        }
        else
        {
            cleaned.push_back('_');
        }
    }

    if (cleaned.empty())
    {
        cleaned = "upload_image";
    }

    return cleaned;
}

std::string WebServer::unique_upload_filename(const std::string &directory_name, const std::string &filename) const
{
    std::string safe_name = sanitize_upload_filename(filename);
    std::string ext;
    std::string stem = safe_name;
    const std::size_t dot = safe_name.find_last_of('.');
    if (dot != std::string::npos)
    {
        stem = safe_name.substr(0, dot);
        ext = safe_name.substr(dot);
    }

    if (stem.empty())
    {
        stem = "upload_image";
    }

    std::ostringstream candidate;
    candidate << stem << ext;
    const std::string target_dir = join_path(m_root, directory_name);
    if (!file_exists(join_path(target_dir, candidate.str())))
    {
        return candidate.str();
    }

    const long timestamp = static_cast<long>(time(nullptr));
    for (int i = 0; i < 1000; ++i)
    {
        std::ostringstream next;
        next << stem << "_" << timestamp << "_" << i << ext;
        if (!file_exists(join_path(target_dir, next.str())))
        {
            return next.str();
        }
    }

    return stem + "_upload" + ext;
}

bool WebServer::ensure_directory_exists(const std::string &path) const
{
    if (is_directory(path))
    {
        return true;
    }

    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string WebServer::build_images_json() const
{
    struct ImageEntry
    {
        std::string name;
        time_t modified_at;
    };

    const std::string directory_path = join_path(m_root, "images");
    DIR *dir = opendir(directory_path.c_str());
    if (dir == nullptr)
    {
        return "[]";
    }

    std::vector<ImageEntry> entries;
    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr)
    {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
        {
            continue;
        }

        const std::string full_path = join_path(directory_path, name);
        if (!file_exists(full_path) || !is_image_extension(name))
        {
            continue;
        }

        struct stat st;
        std::memset(&st, 0, sizeof(st));
        if (stat(full_path.c_str(), &st) != 0)
        {
            continue;
        }

        ImageEntry image_entry;
        image_entry.name = name;
        image_entry.modified_at = st.st_mtime;
        entries.push_back(image_entry);
    }

    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const ImageEntry &left, const ImageEntry &right) {
        if (left.modified_at != right.modified_at)
        {
            return left.modified_at > right.modified_at;
        }
        return left.name < right.name;
    });

    std::ostringstream body;
    body << "[";
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::string url = "/images/" + entries[i].name;
        body << "{"
             << "\"title\":\"" << json_escape_string(entries[i].name) << "\","
             << "\"url\":\"" << json_escape_string(url) << "\","
             << "\"path\":\"" << json_escape_string(url) << "\""
             << "}";
        if (i + 1 < entries.size())
        {
            body << ",";
        }
    }
    body << "]";
    return body.str();
}

std::string WebServer::build_videos_json() const
{
    struct VideoEntry
    {
        std::string name;
        time_t modified_at;
    };

    const std::string directory_path = join_path(m_root, "videos");
    DIR *dir = opendir(directory_path.c_str());
    if (dir == nullptr)
    {
        return "[]";
    }

    std::vector<VideoEntry> entries;
    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr)
    {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
        {
            continue;
        }

        const std::string full_path = join_path(directory_path, name);
        if (!file_exists(full_path) || !is_video_extension(name))
        {
            continue;
        }

        struct stat st;
        std::memset(&st, 0, sizeof(st));
        if (stat(full_path.c_str(), &st) != 0)
        {
            continue;
        }

        VideoEntry video_entry;
        video_entry.name = name;
        video_entry.modified_at = st.st_mtime;
        entries.push_back(video_entry);
    }

    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const VideoEntry &left, const VideoEntry &right) {
        if (left.modified_at != right.modified_at)
        {
            return left.modified_at > right.modified_at;
        }
        return left.name < right.name;
    });

    std::ostringstream body;
    body << "[";
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::string url = "/videos/" + entries[i].name;
        body << "{"
             << "\"title\":\"" << json_escape_string(entries[i].name) << "\","
             << "\"url\":\"" << json_escape_string(url) << "\","
             << "\"path\":\"" << json_escape_string(url) << "\""
             << "}";
        if (i + 1 < entries.size())
        {
            body << ",";
        }
    }
    body << "]";
    return body.str();
}

bool WebServer::save_uploaded_image(const HttpConn::Request &request,
                                    std::string &saved_path,
                                    std::string &detail) const
{
    return save_uploaded_media(request, "images", "image", true, saved_path, detail);
}

bool WebServer::save_uploaded_media(const HttpConn::Request &request,
                                    const std::string &directory_name,
                                    const std::string &field_name,
                                    bool expect_image,
                                    std::string &saved_path,
                                    std::string &detail) const
{
    const std::size_t max_upload_size = expect_image ? kMaxImageUploadSize : kMaxVideoUploadSize;
    const char *max_upload_label = expect_image ? "20MB" : "1GB";

    if (request.body.size() > max_upload_size + 4096)
    {
        detail = std::string(expect_image ? "image is too large, max size is "
                                          : "video is too large, max size is ") +
                 max_upload_label;
        return false;
    }

    const std::string content_type = get_header_value(request, "Content-Type");
    const std::string boundary_key = "boundary=";
    const std::size_t boundary_pos = content_type.find(boundary_key);
    if (boundary_pos == std::string::npos)
    {
        detail = "missing multipart boundary";
        return false;
    }

    std::string boundary = "--" + content_type.substr(boundary_pos + boundary_key.size());
    const std::size_t semicolon = boundary.find(';');
    if (semicolon != std::string::npos)
    {
        boundary = boundary.substr(0, semicolon);
    }

    const std::size_t first_boundary = request.body.find(boundary);
    if (first_boundary == std::string::npos)
    {
        detail = "invalid multipart body";
        return false;
    }

    const std::size_t header_start = first_boundary + boundary.size() + 2;
    const std::size_t header_end = request.body.find("\r\n\r\n", header_start);
    if (header_end == std::string::npos)
    {
        detail = "multipart headers are incomplete";
        return false;
    }

    const std::string part_headers = request.body.substr(header_start, header_end - header_start);
    const std::string name_key = "name=\"";
    const std::size_t field_pos = part_headers.find(name_key);
    if (field_pos == std::string::npos)
    {
        detail = "multipart field is missing";
        return false;
    }
    const std::size_t field_start = field_pos + name_key.size();
    const std::size_t field_end = part_headers.find('"', field_start);
    if (field_end == std::string::npos)
    {
        detail = "invalid multipart field";
        return false;
    }
    const std::string parsed_field_name = part_headers.substr(field_start, field_end - field_start);
    if (parsed_field_name != field_name)
    {
        detail = "unexpected multipart field";
        return false;
    }

    const std::string filename_key = "filename=\"";
    const std::size_t filename_pos = part_headers.find(filename_key);
    if (filename_pos == std::string::npos)
    {
        detail = "no file was selected";
        return false;
    }

    const std::size_t filename_start = filename_pos + filename_key.size();
    const std::size_t filename_end = part_headers.find('"', filename_start);
    if (filename_end == std::string::npos)
    {
        detail = "invalid upload filename";
        return false;
    }

    const std::string raw_filename = part_headers.substr(filename_start, filename_end - filename_start);
    if (raw_filename.empty())
    {
        detail = "no file was selected";
        return false;
    }

    const std::string safe_filename = sanitize_upload_filename(raw_filename);
    const bool ext_ok = expect_image ? is_safe_upload_extension(safe_filename)
                                     : is_safe_video_upload_extension(safe_filename);
    if (!ext_ok)
    {
        detail = expect_image
                     ? "only png, jpg, jpeg, gif, bmp, webp, svg are supported"
                     : "only mp4, webm, ogg, mov, avi, mkv, m4v are supported";
        return false;
    }

    const std::size_t data_start = header_end + 4;
    const std::string end_boundary = "\r\n" + boundary;
    const std::size_t data_end = request.body.find(end_boundary, data_start);
    if (data_end == std::string::npos || data_end < data_start)
    {
        detail = "multipart file data is incomplete";
        return false;
    }

    if (data_end - data_start > max_upload_size)
    {
        detail = std::string(expect_image ? "image is too large, max size is "
                                          : "video is too large, max size is ") +
                 max_upload_label;
        return false;
    }

    const std::string directory_path = join_path(m_root, directory_name);
    if (!ensure_directory_exists(directory_path))
    {
        detail = expect_image ? "failed to create images directory"
                              : "failed to create videos directory";
        return false;
    }

    const std::string final_name = unique_upload_filename(directory_name, safe_filename);
    const std::string full_path = join_path(directory_path, final_name);
    std::ofstream output(full_path.c_str(), std::ios::out | std::ios::binary);
    if (!output.is_open())
    {
        detail = expect_image ? "failed to open target image file"
                              : "failed to open target video file";
        return false;
    }

    output.write(request.body.data() + static_cast<std::string::difference_type>(data_start),
                 static_cast<std::streamsize>(data_end - data_start));
    output.close();

    if (!output)
    {
        detail = expect_image ? "failed to save uploaded image"
                              : "failed to save uploaded video";
        return false;
    }

    saved_path = "/" + directory_name + "/" + final_name;
    detail.clear();
    return true;
}

HttpConn::Response WebServer::handle_upload_api(const HttpConn::Request &request) const
{
    std::string saved_path;
    std::string detail;
    if (!save_uploaded_image(request, saved_path, detail))
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(detail) + "\"}");
    }

    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    std::string("{\"ok\":true,\"message\":\"upload success\",\"path\":\"") +
                                        json_escape_string(saved_path) + "\"}");
}

HttpConn::Response WebServer::handle_upload_video_api(const HttpConn::Request &request) const
{
    std::string saved_path;
    std::string detail;
    if (!save_uploaded_media(request, "videos", "video", false, saved_path, detail))
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(detail) + "\"}");
    }

    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    std::string("{\"ok\":true,\"message\":\"upload success\",\"path\":\"") +
                                        json_escape_string(saved_path) + "\"}");
}

HttpConn::Response WebServer::handle_list_images_api() const
{
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    build_images_json());
}

HttpConn::Response WebServer::handle_list_videos_api() const
{
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    build_videos_json());
}

HttpConn::Response WebServer::handle_login_api(const HttpConn::Request &request) const
{
    const std::map<std::string, std::string> form = m_http_conn.parse_form_urlencoded(request.body);
    const std::string username = form.count("username") ? form.find("username")->second : "";
    const std::string password = form.count("password") ? form.find("password")->second : "";
    const bool remember = form.count("remember") && form.find("remember")->second == "on";

    std::string db_detail;
    if (validate_user_with_db(username, password, db_detail))
    {
        std::ostringstream body;
        body << "{"
             << "\"ok\":true,"
             << "\"message\":\"database login success\","
             << "\"redirect\":\"/app.html\","
             << "\"remember\":" << (remember ? "true" : "false") << ","
             << "\"source\":\"mysql\""
             << "}";
        return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
    }

    if (username == "admin" && password == "12345")
    {
        std::ostringstream body;
        body << "{"
             << "\"ok\":true,"
             << "\"message\":\"fallback login success\","
             << "\"redirect\":\"/app.html\","
             << "\"remember\":" << (remember ? "true" : "false") << ","
             << "\"source\":\"fallback\""
             << "}";
        return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
    }

    const std::string message = db_detail.empty() ? "username or password is invalid"
                                                  : "username or password is invalid; db detail: " + db_detail;
    return build_response_with_body(401,
                                    "Unauthorized",
                                    "application/json; charset=utf-8",
                                    std::string("{\"ok\":false,\"message\":\"") + json_escape(message) +
                                        "\",\"redirect\":\"/login.html\"}");
}

HttpConn::Response WebServer::handle_register_api(const HttpConn::Request &request) const
{
    const std::map<std::string, std::string> form = m_http_conn.parse_form_urlencoded(request.body);
    const std::string phone = form.count("phone") ? form.find("phone")->second : "";
    const std::string email = form.count("email") ? form.find("email")->second : "";
    const std::string email_code = form.count("email_code") ? form.find("email_code")->second : "";
    const std::string password = form.count("password") ? form.find("password")->second : "";
    std::string detail;

    if (!is_valid_phone(phone))
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"phone number is invalid\"}");
    }

    if (!is_valid_email(email))
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"email is invalid\"}");
    }

    if (password.size() < 4)
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"password must be at least 4 characters\"}");
    }

    if (!verify_email_code(email, email_code, detail))
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") + json_escape(detail) + "\"}");
    }

    if (!register_user_with_db(phone, password, detail))
    {
        const int status = detail == "username already exists" ? 409 : 500;
        const std::string status_text = detail == "username already exists" ? "Conflict" : "Internal Server Error";
        if (detail == "username already exists")
        {
            detail = "phone number already registered";
        }
        return build_response_with_body(status,
                                        status_text,
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") + json_escape(detail) + "\"}");
    }

    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    std::string("{\"ok\":true,\"message\":\"") +
                                        json_escape("registration success, you can log in with your phone number now") +
                                        "\",\"username\":\"" + json_escape(phone) + "\"}");
}

HttpConn::Response WebServer::handle_send_email_code_api(const HttpConn::Request &request) const
{
    const std::map<std::string, std::string> form = m_http_conn.parse_form_urlencoded(request.body);
    const std::string email = form.count("email") ? form.find("email")->second : "";

    if (!is_valid_email(email))
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"email is invalid\"}");
    }

    const std::string code = generate_email_verification_code();
    std::string detail;
    if (!SmtpClient::send_verification_code(email, code, detail))
    {
        std::cerr << "SMTP send failed for " << email << ": " << detail << std::endl;
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"verification code email failed: ") +
                                            json_escape(detail) + "\"}");
    }

    save_email_verification_code(email, code);

    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    "{\"ok\":true,\"message\":\"verification code sent\"}");
}

HttpConn::Response WebServer::handle_reset_api(const HttpConn::Request &request) const
{
    (void)request;
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    "{\"ok\":true,\"message\":\"reset API placeholder is connected; next step is verification logic.\"}");
}

HttpConn::Response WebServer::handle_request(const HttpConn::Request &request) const
{
    if (!is_safe_path(request.path))
    {
        return build_error_response(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
    }

    if (request.method == "POST" && request.path == "/api/login")
    {
        return handle_login_api(request);
    }
    if (request.method == "POST" && request.path == "/api/register")
    {
        return handle_register_api(request);
    }
    if (request.method == "POST" && request.path == "/api/send-email-code")
    {
        return handle_send_email_code_api(request);
    }
    if (request.method == "POST" && request.path == "/api/reset")
    {
        return handle_reset_api(request);
    }
    if (request.method == "POST" && request.path == "/api/upload")
    {
        return handle_upload_api(request);
    }
    if (request.method == "POST" && request.path == "/api/upload-video")
    {
        return handle_upload_video_api(request);
    }
    if (request.method == "GET" && request.path == "/api/images")
    {
        return handle_list_images_api();
    }
    if (request.method == "GET" && request.path == "/api/videos")
    {
        return handle_list_videos_api();
    }

    if (request.method != "GET")
    {
        return build_error_response(405, "Method Not Allowed", "<html><body><h1>405 Method Not Allowed</h1></body></html>");
    }

    std::string effective_path = request.path;
    if (effective_path == "/")
    {
        const std::string login_path = m_root + "/login.html";
        const std::string index_path = m_root + "/index.html";
        if (file_exists(login_path))
        {
            effective_path = "/login.html";
        }
        else if (file_exists(index_path))
        {
            effective_path = "/index.html";
        }
    }

    const std::string full_path = m_root + effective_path;
    if (is_directory(full_path))
    {
        const std::string index_path = join_path(full_path, "index.html");
        if (file_exists(index_path))
        {
            return build_response_with_body(200, "OK", "text/html; charset=utf-8", read_file(index_path));
        }
        return build_response_with_body(200, "OK", "text/html; charset=utf-8",
                                        build_directory_listing(effective_path, full_path));
    }

    if (!file_exists(full_path))
    {
        return build_error_response(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
    }

    return build_response_with_body(200, "OK", get_content_type(full_path), read_file(full_path));
}

bool WebServer::read_http_request(int sockfd, std::string &request_text)
{
    char buf[kReadBufferSize];
    {
        std::unique_lock<std::mutex> lock(m_pending_mutex);
        request_text = m_pending_requests[sockfd];
    }

    while (true)
    {
        std::memset(buf, 0, sizeof(buf));
        const int ret = recv(sockfd, buf, sizeof(buf), 0);
        if (ret > 0)
        {
            request_text.append(buf, ret);
            if (request_is_complete(request_text))
            {
                remove_pending_request(sockfd);
                return true;
            }
            continue;
        }

        if (ret == 0)
        {
            const bool complete = request_is_complete(request_text);
            remove_pending_request(sockfd);
            if (!complete)
            {
                request_text.clear();
            }
            return complete;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            std::unique_lock<std::mutex> lock(m_pending_mutex);
            m_pending_requests[sockfd] = request_text;
            return false;
        }
        remove_pending_request(sockfd);
        request_text.clear();
        return false;
    }

    return false;
}

void WebServer::dealwithread(int sockfd)
{
    adjust_timer(sockfd);

    std::string request_text;
    const bool request_ready = read_http_request(sockfd, request_text);
    if (!request_ready)
    {
        if (request_text.empty())
        {
            closefd(sockfd);
            return;
        }
        if (!modfd(sockfd, EPOLLIN))
        {
            closefd(sockfd);
        }
        return;
    }

    if (request_text.empty())
    {
        closefd(sockfd);
        return;
    }

    if (m_actormodel == 1)
    {
        try
        {
            m_thread_pool->enqueue(std::bind(&WebServer::process_request, this, sockfd, request_text));
        }
        catch (const std::exception &)
        {
            closefd(sockfd);
        }
        return;
    }

    process_request(sockfd, request_text);
}

void WebServer::process_request(int sockfd, const std::string &request_text)
{
    if (request_text.empty())
    {
        closefd(sockfd);
        return;
    }

    HttpConn::Request request;
    if (!m_http_conn.parse_request(request_text, request))
    {
        const HttpConn::Response bad_request =
            build_error_response(400, "Bad Request", "<html><body><h1>400 Bad Request</h1></body></html>");
        const std::string raw = bad_request.to_http_string();
        send_all(sockfd, raw);
        closefd(sockfd);
        return;
    }

    const HttpConn::Response response = handle_request(request);
    const std::string raw = response.to_http_string();
    send_all(sockfd, raw);
    closefd(sockfd);
}

bool WebServer::send_all(int sockfd, const std::string &data) const
{
    std::size_t total_sent = 0;
    while (total_sent < data.size())
    {
        const ssize_t sent = send(sockfd,
                                  data.c_str() + total_sent,
                                  data.size() - total_sent,
                                  0);
        if (sent > 0)
        {
            total_sent += static_cast<std::size_t>(sent);
            continue;
        }

        if (sent < 0 && (errno == EINTR))
        {
            continue;
        }

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            continue;
        }

        return false;
    }
    return true;
}

void WebServer::eventLoop()
{
    while (true)
    {
        const int number = epoll_wait(m_epollfd, m_events, MAX_EVENT_NUMBER, -1);
        if (number < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < number; ++i)
        {
            const int sockfd = m_events[i].data.fd;
            if (sockfd == m_listenfd)
            {
                dealclientdata();
            }
            else if (sockfd == m_pipefd[0] && (m_events[i].events & EPOLLIN))
            {
                int signals[1024];
                while (true)
                {
                    const ssize_t ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
                    if (ret <= 0)
                    {
                        break;
                    }

                    const int count = static_cast<int>(ret / static_cast<ssize_t>(sizeof(int)));
                    for (int j = 0; j < count; ++j)
                    {
                        if (signals[j] == SIGALRM)
                        {
                            timer_handler();
                        }
                    }
                }
            }
            else if (m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                deal_timer(sockfd);
            }
            else if (m_events[i].events & EPOLLIN)
            {
                if (m_actormodel == 1)
                {
                    dealwithread(sockfd);
                }
                else
                {
                    try
                    {
                        m_thread_pool->enqueue(std::bind(&WebServer::dealwithread, this, sockfd));
                    }
                    catch (const std::exception &)
                    {
                        closefd(sockfd);
                    }
                }
            }
        }
    }
}
