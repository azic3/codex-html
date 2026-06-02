#include "webserver.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
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
const int kEmailCodeRequestInterval = 60;
const int kEmailCodeIpMinuteLimit = 5;
const int kEmailCodeDailyWindow = 24 * 60 * 60;
const int kEmailCodeDailyLimit = 10;
const int kEmailCodeMaxFailedAttempts = 5;
const int kMediaListCacheTimeout = 60;
const int kSessionDefaultTtl = 2 * 60 * 60;
const int kSessionRememberTtl = 7 * 24 * 60 * 60;
const std::size_t kDefaultImagePageSize = 12;
const std::size_t kMaxImagePageSize = 100;
const std::size_t kDefaultCommentPageSize = 20;
const std::size_t kMaxCommentPageSize = 50;
const std::size_t kMaxCommentLength = 300;
const std::size_t kVideoChunkSize = 2 * 1024 * 1024;
const std::size_t kMaxVideoChunkBodySize = 4 * 1024 * 1024;
const std::size_t kDefaultMaxImageUploadSize = 20 * 1024 * 1024;
const std::size_t kDefaultMaxVideoUploadSize = 1024ULL * 1024 * 1024;
const unsigned long long kVideoRangeChunkSize = 1024ULL * 1024ULL;
const char *kSessionCookieName = "XIAOCHEN_SESSION";
const char *kRedisEmailCodePrefix = "xiaochen:email_code:";
const char *kRedisEmailCodeFailedPrefix = "xiaochen:email_code_failed:";
const char *kRedisEmailCodePhoneDailyPrefix = "xiaochen:email_code_daily:phone:";
const char *kRedisEmailCodeIpDailyPrefix = "xiaochen:email_code_daily:ip:";
const char *kRedisEmailCodePhoneMinutePrefix = "xiaochen:email_code_minute:phone:";
const char *kRedisEmailCodeIpMinutePrefix = "xiaochen:email_code_minute:ip:";
const char *kRedisImagesListKey = "xiaochen:cache:images:list:v2";
const char *kRedisVideosListKey = "xiaochen:cache:videos:list:v2";
int g_signal_pipefd[2] = {-1, -1};

enum RangeParseStatus
{
    RANGE_NOT_REQUESTED,
    RANGE_VALID,
    RANGE_INVALID
};

std::string trim_whitespace(const std::string &value)
{
    std::size_t start = 0;
    while (start < value.size() &&
           (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n'))
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
    {
        --end;
    }

    return value.substr(start, end - start);
}

bool parse_unsigned_number(const std::string &value, unsigned long long &number)
{
    if (value.empty())
    {
        return false;
    }

    unsigned long long parsed = 0;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[i])))
        {
            return false;
        }

        const unsigned int digit = static_cast<unsigned int>(value[i] - '0');
        if (parsed > (ULLONG_MAX - digit) / 10)
        {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    number = parsed;
    return true;
}

std::size_t utf8_codepoint_count(const std::string &value)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if ((ch & 0xC0) != 0x80)
        {
            ++count;
        }
    }
    return count;
}

std::size_t utf8_safe_prefix_length(const std::string &value, std::size_t max_bytes)
{
    if (value.size() <= max_bytes)
    {
        return value.size();
    }

    std::size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80)
    {
        --end;
    }

    return end;
}

bool has_query_param(const std::string &target, const std::string &name)
{
    const std::size_t query_pos = target.find('?');
    if (query_pos == std::string::npos)
    {
        return false;
    }

    std::size_t start = query_pos + 1;
    while (start <= target.size())
    {
        const std::size_t amp = target.find('&', start);
        const std::string pair = target.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        const std::size_t eq = pair.find('=');
        if ((eq == std::string::npos ? pair : pair.substr(0, eq)) == name)
        {
            return true;
        }

        if (amp == std::string::npos)
        {
            break;
        }
        start = amp + 1;
    }

    return false;
}

std::size_t query_param_size(const std::string &target,
                             const std::string &name,
                             std::size_t fallback,
                             std::size_t min_value,
                             std::size_t max_value)
{
    const std::size_t query_pos = target.find('?');
    if (query_pos == std::string::npos)
    {
        return fallback;
    }

    std::size_t start = query_pos + 1;
    while (start <= target.size())
    {
        const std::size_t amp = target.find('&', start);
        const std::string pair = target.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        const std::size_t eq = pair.find('=');
        const std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
        if (key == name)
        {
            unsigned long long parsed = 0;
            if (eq != std::string::npos && parse_unsigned_number(pair.substr(eq + 1), parsed))
            {
                const std::size_t value = static_cast<std::size_t>(parsed);
                return std::max(min_value, std::min(max_value, value));
            }
            return fallback;
        }

        if (amp == std::string::npos)
        {
            break;
        }
        start = amp + 1;
    }

    return fallback;
}

bool parse_image_reaction_path(const std::string &path, unsigned long long &image_id, std::string &action)
{
    const std::string prefix = "/api/images/";
    if (path.find(prefix) != 0)
    {
        return false;
    }

    const std::size_t action_slash = path.find('/', prefix.size());
    if (action_slash == std::string::npos)
    {
        return false;
    }

    const std::string id_text = path.substr(prefix.size(), action_slash - prefix.size());
    if (!parse_unsigned_number(id_text, image_id) || image_id == 0)
    {
        return false;
    }

    action = path.substr(action_slash + 1);
    return action == "like" || action == "favorite";
}

bool parse_image_download_path(const std::string &path, unsigned long long &image_id)
{
    std::string action;
    const std::string prefix = "/api/images/";
    if (path.find(prefix) != 0)
    {
        return false;
    }

    const std::size_t action_slash = path.find('/', prefix.size());
    if (action_slash == std::string::npos)
    {
        return false;
    }

    const std::string id_text = path.substr(prefix.size(), action_slash - prefix.size());
    if (!parse_unsigned_number(id_text, image_id) || image_id == 0)
    {
        return false;
    }

    action = path.substr(action_slash + 1);
    return action == "download";
}

bool parse_image_comments_path(const std::string &path, unsigned long long &image_id)
{
    const std::string prefix = "/api/images/";
    const std::string suffix = "/comments";
    if (path.find(prefix) != 0 || path.size() <= prefix.size() + suffix.size())
    {
        return false;
    }

    if (path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return false;
    }

    const std::string id_text = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    return parse_unsigned_number(id_text, image_id) && image_id != 0;
}

bool parse_image_comment_path(const std::string &path, unsigned long long &comment_id)
{
    const std::string prefix = "/api/comments/";
    if (path.find(prefix) != 0 || path.size() == prefix.size())
    {
        return false;
    }

    const std::string id_text = path.substr(prefix.size());
    return parse_unsigned_number(id_text, comment_id) && comment_id != 0;
}

std::string attachment_disposition_header(const std::string &filename, const std::string &encoded_filename)
{
    std::string fallback;
    fallback.reserve(filename.size());
    for (std::size_t i = 0; i < filename.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(filename[i]);
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.')
        {
            fallback.push_back(static_cast<char>(ch));
        }
        else
        {
            fallback.push_back('_');
        }
    }

    if (fallback.empty())
    {
        fallback = "image";
    }

    return "attachment; filename=\"" + fallback + "\"; filename*=UTF-8''" + encoded_filename;
}

std::string redis_email_code_key(const std::string &phone, const std::string &email)
{
    return std::string(kRedisEmailCodePrefix) + phone + ":" + email;
}

std::string redis_email_failed_key(const std::string &phone, const std::string &email)
{
    return std::string(kRedisEmailCodeFailedPrefix) + phone + ":" + email;
}

std::string redis_phone_rate_key(const std::string &phone)
{
    return std::string(kRedisEmailCodePhoneDailyPrefix) + phone;
}

std::string redis_ip_rate_key(const std::string &ip)
{
    return std::string(kRedisEmailCodeIpDailyPrefix) + (ip.empty() ? "unknown" : ip);
}

std::string redis_phone_minute_key(const std::string &phone)
{
    return std::string(kRedisEmailCodePhoneMinutePrefix) + phone;
}

std::string redis_ip_minute_key(const std::string &ip)
{
    return std::string(kRedisEmailCodeIpMinutePrefix) + (ip.empty() ? "unknown" : ip);
}

RangeParseStatus parse_range_header(const std::string &header,
                                    unsigned long long total_size,
                                    unsigned long long &start,
                                    unsigned long long &end)
{
    if (header.empty())
    {
        return RANGE_NOT_REQUESTED;
    }

    std::string value = trim_whitespace(header);
    for (std::size_t i = 0; i < value.size() && i < 6; ++i)
    {
        value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
    }

    const std::string prefix = "bytes=";
    if (value.compare(0, prefix.size(), prefix) != 0)
    {
        return RANGE_NOT_REQUESTED;
    }

    const std::string range_spec = trim_whitespace(value.substr(prefix.size()));
    if (range_spec.find(',') != std::string::npos || total_size == 0)
    {
        return RANGE_INVALID;
    }

    const std::size_t dash = range_spec.find('-');
    if (dash == std::string::npos)
    {
        return RANGE_INVALID;
    }

    const std::string start_text = trim_whitespace(range_spec.substr(0, dash));
    const std::string end_text = trim_whitespace(range_spec.substr(dash + 1));
    if (start_text.empty() && end_text.empty())
    {
        return RANGE_INVALID;
    }

    if (start_text.empty())
    {
        unsigned long long suffix_length = 0;
        if (!parse_unsigned_number(end_text, suffix_length) || suffix_length == 0)
        {
            return RANGE_INVALID;
        }

        start = suffix_length >= total_size ? 0 : total_size - suffix_length;
        end = total_size - 1;
        return RANGE_VALID;
    }

    if (!parse_unsigned_number(start_text, start))
    {
        return RANGE_INVALID;
    }

    if (end_text.empty())
    {
        end = total_size - 1;
    }
    else if (!parse_unsigned_number(end_text, end))
    {
        return RANGE_INVALID;
    }

    if (start >= total_size || start > end)
    {
        return RANGE_INVALID;
    }

    if (end >= total_size)
    {
        end = total_size - 1;
    }

    return RANGE_VALID;
}

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

std::string format_byte_limit(std::size_t bytes)
{
    const std::size_t gb = 1024ULL * 1024ULL * 1024ULL;
    const std::size_t mb = 1024ULL * 1024ULL;
    const std::size_t kb = 1024ULL;
    std::ostringstream label;

    if (bytes >= gb && bytes % gb == 0)
    {
        label << (bytes / gb) << "GB";
    }
    else if (bytes >= mb && bytes % mb == 0)
    {
        label << (bytes / mb) << "MB";
    }
    else if (bytes >= kb && bytes % kb == 0)
    {
        label << (bytes / kb) << "KB";
    }
    else
    {
        label << bytes << " bytes";
    }

    return label.str();
}

std::string random_session_token()
{
    std::ostringstream token;
    token << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i)
    {
        token << std::setw(8) << static_cast<unsigned int>(std::rand());
    }
    token << std::setw(8) << static_cast<unsigned int>(time(nullptr));
    return token.str();
}

std::string cookie_value(const std::string &cookie_header, const std::string &name)
{
    std::size_t start = 0;
    while (start < cookie_header.size())
    {
        while (start < cookie_header.size() && (cookie_header[start] == ' ' || cookie_header[start] == ';'))
        {
            ++start;
        }

        const std::size_t end = cookie_header.find(';', start);
        const std::string pair = cookie_header.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::size_t equals = pair.find('=');
        if (equals != std::string::npos && pair.substr(0, equals) == name)
        {
            return pair.substr(equals + 1);
        }

        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }

    return std::string();
}

bool is_auth_api(const HttpConn::Request &request)
{
    return (request.method == "POST" && request.path == "/api/login") ||
           (request.method == "POST" && request.path == "/api/register") ||
           (request.method == "POST" && request.path == "/api/send-email-code") ||
           (request.method == "POST" && request.path == "/api/reset") ||
           (request.method == "POST" && request.path == "/api/logout");
}

bool is_public_browse_api(const HttpConn::Request &request)
{
    return (request.method == "GET" && request.path == "/api/images") ||
           (request.method == "GET" && request.path == "/api/videos");
}

bool is_protected_api(const HttpConn::Request &request)
{
    return request.path.find("/api/") == 0 &&
           !is_auth_api(request) &&
           !is_public_browse_api(request);
}

bool is_protected_page_path(const std::string &path)
{
    if (path == "/images/\xE5\x87\xA1\xE4\xBA\xBA\xE4\xBF\xAE\xE4\xBB\x99_001.png")
    {
        return false;
    }

    if (path == "/app.html" ||
        path == "/home.html" ||
        path == "/video.html" ||
        path.find("/media/images/") == 0 ||
        path.find("/media/videos/") == 0)
    {
        return false;
    }

    return path == "/game.html" ||
           path == "/profile.html" ||
           path == "/index.html" ||
           path == "/images" ||
           path == "/images/" ||
           path == "/videos" ||
           path == "/videos/" ||
           path.find("/videos/") == 0;
}

std::string session_cookie_header(const std::string &token, bool remember)
{
    const int ttl = remember ? kSessionRememberTtl : kSessionDefaultTtl;
    return std::string(kSessionCookieName) + "=" + token +
           "; HttpOnly; Path=/; SameSite=Lax; Max-Age=" + std::to_string(ttl);
}

HttpConn::Response build_login_redirect_response()
{
    HttpConn::Response response;
    response.status_code = 302;
    response.status_text = "Found";
    response.content_type = "text/html; charset=utf-8";
    response.body = "<html><body><a href=\"/login.html\">Login</a></body></html>";
    response.headers["Location"] = "/login.html";
    return response;
}

HttpConn::Response build_unauthorized_json_response()
{
    HttpConn::Response response;
    response.status_code = 401;
    response.status_text = "Unauthorized";
    response.content_type = "application/json; charset=utf-8";
    response.body = "{\"ok\":false,\"message\":\"not logged in\",\"redirect\":\"/login.html\"}";
    return response;
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
      m_db_port(3306),
      m_db_user("root"),
      m_db_password("password"),
      m_db_name("qgydb"),
      m_max_image_upload_size(kDefaultMaxImageUploadSize),
      m_max_video_upload_size(kDefaultMaxVideoUploadSize)
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
    m_db_host = databaseHost.empty() ? "127.0.0.1" : databaseHost;
    m_db_port = databasePort > 0 ? databasePort : 3306;
    m_root = staticRoot;
    m_max_image_upload_size = maxImageUploadSize > 0 ? maxImageUploadSize : kDefaultMaxImageUploadSize;
    m_max_video_upload_size = maxVideoUploadSize > 0 ? maxVideoUploadSize : kDefaultMaxVideoUploadSize;

    trig_mode();
    m_db_pool.init(m_db_host, m_db_port, m_db_user, m_db_password, m_db_name, m_sql_num);
    m_redis.init_from_env();
    AppLogger::info(std::string("redis cache ") + (m_redis.enabled() ? "enabled" : "disabled"));
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
    if ((fd == m_listenfd && m_LISTENTrigmode == 1) ||
        (fd != m_listenfd && m_CONNTrigmode == 1))
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

    if (m_root.empty())
    {
        char cwd[512] = {0};
        if (getcwd(cwd, sizeof(cwd)) != nullptr)
        {
            m_root = std::string(cwd) + "/public";
        }
        else
        {
            m_root = "public";
        }
    }

    AppLogger::info("server listening on 0.0.0.0:" + std::to_string(m_port));
    AppLogger::info("static root: " + m_root);
    AppLogger::info("upload limits image=" + format_byte_limit(m_max_image_upload_size) +
                    " video=" + format_byte_limit(m_max_video_upload_size));
    if (!m_db_pool.available())
    {
        AppLogger::error("MySQL pool unavailable: " + m_db_pool.last_error());
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
    cleanup_expired_email_state(time(nullptr));

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

std::string WebServer::read_file_range(const std::string &path, unsigned long long start, unsigned long long length) const
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open())
    {
        return std::string();
    }

    input.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    if (!input.good())
    {
        return std::string();
    }

    std::string data;
    data.resize(static_cast<std::size_t>(length));
    input.read(&data[0], static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(input.gcount()));
    return data;
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

std::string WebServer::url_encode_path_segment(const std::string &value) const
{
    const char *hex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            encoded.push_back(static_cast<char>(ch));
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }

    return encoded;
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
        href += url_encode_path_segment(name);
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

HttpConn::Response WebServer::build_static_file_response(const HttpConn::Request &request, const std::string &path) const
{
    const std::string content_type = get_content_type(path);
    HttpConn::Response response;
    response.content_type = content_type;

    if (!is_video_extension(path))
    {
        return build_response_with_body(200, "OK", content_type, read_file(path));
    }

    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    {
        return build_error_response(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
    }

    const unsigned long long total_size = static_cast<unsigned long long>(st.st_size);
    unsigned long long start = 0;
    unsigned long long end = 0;
    const RangeParseStatus range_status = parse_range_header(get_header_value(request, "Range"), total_size, start, end);

    response.headers["Accept-Ranges"] = "bytes";

    if (range_status == RANGE_INVALID)
    {
        response.status_code = 416;
        response.status_text = "Range Not Satisfiable";
        response.body.clear();
        response.headers["Content-Range"] = "bytes */" + std::to_string(total_size);
        return response;
    }

    if (range_status == RANGE_VALID)
    {
        if (end >= start && end - start + 1 > kVideoRangeChunkSize)
        {
            end = start + kVideoRangeChunkSize - 1;
            if (end >= total_size)
            {
                end = total_size - 1;
            }
        }

        const unsigned long long length = end - start + 1;
        response.status_code = 206;
        response.status_text = "Partial Content";
        response.body = read_file_range(path, start, length);
        if (static_cast<unsigned long long>(response.body.size()) != length)
        {
            return build_error_response(500, "Internal Server Error", "<html><body><h1>500 Internal Server Error</h1></body></html>");
        }
        response.headers["Content-Range"] = "bytes " + std::to_string(start) + "-" +
                                            std::to_string(end) + "/" +
                                            std::to_string(total_size);
        return response;
    }

    response.status_code = 200;
    response.status_text = "OK";
    response.body = read_file(path);
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
    std::string password_version;
    if (!m_db_pool.fetch_user_credentials(username, db_password, password_version))
    {
        detail = m_db_pool.last_error();
        return false;
    }

    bool matched = false;
    bool needs_rehash = false;
    if (!PasswordHasher::verify_password(password, db_password, password_version, matched, needs_rehash, detail))
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
            m_db_pool.update_user_password(username, hashed_password, PasswordHasher::current_version());
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

    if (!m_db_pool.insert_user(username, hashed_password, PasswordHasher::current_version()))
    {
        detail = m_db_pool.last_error();
        return false;
    }

    detail.clear();
    return true;
}

bool WebServer::reset_user_password_with_db(const std::string &username,
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

    if (!exists)
    {
        detail = "phone number was not registered";
        return false;
    }

    std::string hashed_password;
    if (!PasswordHasher::hash_password(password, hashed_password, detail))
    {
        return false;
    }

    if (!m_db_pool.update_user_password(username, hashed_password, PasswordHasher::current_version()))
    {
        detail = m_db_pool.last_error();
        return false;
    }

    detail.clear();
    return true;
}

std::string WebServer::create_session(const std::string &username, bool is_admin, bool remember) const
{
    UserSession session;
    session.username = username;
    session.is_admin = is_admin;
    session.created_at = time(nullptr);
    session.expire_at = session.created_at + (remember ? kSessionRememberTtl : kSessionDefaultTtl);

    std::unique_lock<std::mutex> lock(m_session_mutex);
    std::string token;
    do
    {
        token = random_session_token();
    } while (m_sessions.find(token) != m_sessions.end());

    m_sessions[token] = session;
    return token;
}

bool WebServer::get_session(const HttpConn::Request &request, std::string &username, bool &is_admin) const
{
    const std::string cookie_header = get_header_value(request, "Cookie");
    const std::string token = cookie_value(cookie_header, kSessionCookieName);
    if (token.empty())
    {
        return false;
    }

    std::unique_lock<std::mutex> lock(m_session_mutex);
    std::unordered_map<std::string, UserSession>::const_iterator it = m_sessions.find(token);
    if (it == m_sessions.end())
    {
        return false;
    }

    if (it->second.expire_at <= time(nullptr))
    {
        m_sessions.erase(it);
        return false;
    }

    username = it->second.username;
    is_admin = it->second.is_admin;
    return true;
}

void WebServer::destroy_session(const HttpConn::Request &request) const
{
    const std::string cookie_header = get_header_value(request, "Cookie");
    const std::string token = cookie_value(cookie_header, kSessionCookieName);
    if (token.empty())
    {
        return;
    }

    std::unique_lock<std::mutex> lock(m_session_mutex);
    m_sessions.erase(token);
}

bool WebServer::current_user_is_admin(const HttpConn::Request &request) const
{
    std::string username;
    bool is_admin = false;
    return get_session(request, username, is_admin) && is_admin;
}

std::string WebServer::get_client_ip(int sockfd) const
{
    sockaddr_storage address;
    std::memset(&address, 0, sizeof(address));
    socklen_t address_length = sizeof(address);
    if (getpeername(sockfd, reinterpret_cast<sockaddr *>(&address), &address_length) != 0)
    {
        return "unknown";
    }

    char buffer[INET6_ADDRSTRLEN] = {0};
    if (address.ss_family == AF_INET)
    {
        const sockaddr_in *ipv4 = reinterpret_cast<const sockaddr_in *>(&address);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) != nullptr)
        {
            return buffer;
        }
    }
    else if (address.ss_family == AF_INET6)
    {
        const sockaddr_in6 *ipv6 = reinterpret_cast<const sockaddr_in6 *>(&address);
        if (inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer)) != nullptr)
        {
            return buffer;
        }
    }

    return "unknown";
}

std::string WebServer::generate_email_verification_code() const
{
    const int value = 100000 + std::rand() % 900000;
    std::ostringstream code_stream;
    code_stream << value;
    return code_stream.str();
}

std::string WebServer::make_email_code_key(const std::string &phone, const std::string &email) const
{
    return phone + "\n" + email;
}

void WebServer::cleanup_expired_email_state(time_t now) const
{
    std::unique_lock<std::mutex> lock(m_email_code_mutex);

    for (std::unordered_map<std::string, EmailVerificationCode>::iterator it = m_email_codes.begin();
         it != m_email_codes.end();)
    {
        if (it->second.expire < now)
        {
            it = m_email_codes.erase(it);
            continue;
        }
        ++it;
    }

    for (std::unordered_map<std::string, EmailCodeRateState>::iterator it = m_email_code_phone_limits.begin();
         it != m_email_code_phone_limits.end();)
    {
        if (it->second.last_request_at > 0 && now - it->second.last_request_at >= kEmailCodeDailyWindow)
        {
            it = m_email_code_phone_limits.erase(it);
            continue;
        }
        ++it;
    }

    for (std::unordered_map<std::string, EmailCodeRateState>::iterator it = m_email_code_ip_limits.begin();
         it != m_email_code_ip_limits.end();)
    {
        if (it->second.last_request_at > 0 && now - it->second.last_request_at >= kEmailCodeDailyWindow)
        {
            it = m_email_code_ip_limits.erase(it);
            continue;
        }
        ++it;
    }
}

bool WebServer::consume_email_code_rate_limit(const std::string &phone, const std::string &ip, std::string &detail) const
{
    const time_t now = time(nullptr);

    if (m_redis.enabled())
    {
        const std::string phone_key = redis_phone_rate_key(phone);
        const std::string ip_key = redis_ip_rate_key(ip);
        const std::string phone_minute_key = redis_phone_minute_key(phone);
        const std::string ip_minute_key = redis_ip_minute_key(ip);
        long long phone_count = 0;
        long long ip_count = 0;
        long long ip_minute_count = 0;
        std::string minute_marker;

        if (m_redis.get(phone_minute_key, minute_marker))
        {
            detail = "verification code can only be requested once per minute for this phone number";
            return false;
        }
        if (!m_redis.last_error().empty())
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }

        if (!m_redis.incr(ip_minute_key, ip_minute_count))
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }
        if (ip_minute_count == 1 && !m_redis.expire(ip_minute_key, kEmailCodeRequestInterval))
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }
        if (ip_minute_count > kEmailCodeIpMinuteLimit)
        {
            detail = "verification code can only be requested 5 times per minute from this IP";
            return false;
        }

        if (!m_redis.incr(phone_key, phone_count) || !m_redis.incr(ip_key, ip_count))
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }

        if (phone_count == 1 && !m_redis.expire(phone_key, kEmailCodeDailyWindow))
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }
        if (ip_count == 1 && !m_redis.expire(ip_key, kEmailCodeDailyWindow))
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }

        if (phone_count > kEmailCodeDailyLimit)
        {
            detail = "verification code daily request limit reached for this phone number";
            return false;
        }
        if (ip_count > kEmailCodeDailyLimit)
        {
            detail = "verification code daily request limit reached for this IP";
            return false;
        }

        if (!m_redis.setex(phone_minute_key, kEmailCodeRequestInterval, "1"))
        {
            detail = "redis unavailable for verification code rate limit";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }

        detail.clear();
        return true;
    }

    std::unique_lock<std::mutex> lock(m_email_code_mutex);

    EmailCodeRateState &phone_state = m_email_code_phone_limits[phone];
    EmailCodeRateState &ip_state = m_email_code_ip_limits[ip.empty() ? "unknown" : ip];

    if (phone_state.daily_window_start == 0 || now - phone_state.daily_window_start >= kEmailCodeDailyWindow)
    {
        phone_state.daily_window_start = now;
        phone_state.daily_count = 0;
    }
    if (ip_state.daily_window_start == 0 || now - ip_state.daily_window_start >= kEmailCodeDailyWindow)
    {
        ip_state.daily_window_start = now;
        ip_state.daily_count = 0;
    }
    if (ip_state.minute_window_start == 0 || now - ip_state.minute_window_start >= kEmailCodeRequestInterval)
    {
        ip_state.minute_window_start = now;
        ip_state.minute_count = 0;
    }

    if (phone_state.last_request_at > 0 && now - phone_state.last_request_at < kEmailCodeRequestInterval)
    {
        detail = "verification code can only be requested once per minute for this phone number";
        return false;
    }
    if (ip_state.minute_count >= kEmailCodeIpMinuteLimit)
    {
        detail = "verification code can only be requested 5 times per minute from this IP";
        return false;
    }
    if (phone_state.daily_count >= kEmailCodeDailyLimit)
    {
        detail = "verification code daily request limit reached for this phone number";
        return false;
    }
    if (ip_state.daily_count >= kEmailCodeDailyLimit)
    {
        detail = "verification code daily request limit reached for this IP";
        return false;
    }

    phone_state.last_request_at = now;
    ++phone_state.daily_count;
    ip_state.last_request_at = now;
    ++ip_state.minute_count;
    ++ip_state.daily_count;
    detail.clear();
    return true;
}

bool WebServer::save_email_verification_code(const std::string &phone,
                                             const std::string &email,
                                             const std::string &code,
                                             std::string &detail) const
{
    if (m_redis.enabled())
    {
        const std::string code_key = redis_email_code_key(phone, email);
        const std::string failed_key = redis_email_failed_key(phone, email);
        if (!m_redis.setex(code_key, kEmailCodeTimeout, code))
        {
            detail = "redis unavailable for verification code storage";
            AppLogger::error(detail + ": " + m_redis.last_error());
            return false;
        }
        m_redis.del(failed_key);
        detail.clear();
        return true;
    }

    EmailVerificationCode record;
    record.phone = phone;
    record.code = code;
    record.expire = time(nullptr) + kEmailCodeTimeout;
    record.failed_attempts = 0;

    std::unique_lock<std::mutex> lock(m_email_code_mutex);
    m_email_codes[make_email_code_key(phone, email)] = record;
    detail.clear();
    return true;
}

bool WebServer::verify_email_code(const std::string &phone, const std::string &email, const std::string &code, std::string &detail) const
{
    if (!is_valid_phone(phone))
    {
        detail = "phone number is invalid";
        return false;
    }

    if (!is_valid_email(email))
    {
        detail = "email is invalid";
        return false;
    }

    if (m_redis.enabled())
    {
        const std::string code_key = redis_email_code_key(phone, email);
        const std::string failed_key = redis_email_failed_key(phone, email);
        std::string expected_code;
        if (!m_redis.get(code_key, expected_code))
        {
            detail = m_redis.last_error().empty() ? "verification code was not sent"
                                                  : "redis unavailable for verification code lookup";
            if (!m_redis.last_error().empty())
            {
                AppLogger::error(detail + ": " + m_redis.last_error());
            }
            return false;
        }

        if (!is_six_digit_code(code) || expected_code != code)
        {
            long long failed_attempts = 0;
            if (!m_redis.incr(failed_key, failed_attempts))
            {
                detail = "redis unavailable for verification code attempts";
                AppLogger::error(detail + ": " + m_redis.last_error());
                return false;
            }
            if (failed_attempts == 1)
            {
                m_redis.expire(failed_key, kEmailCodeTimeout);
            }
            if (failed_attempts >= kEmailCodeMaxFailedAttempts)
            {
                m_redis.del(code_key);
                m_redis.del(failed_key);
                detail = "verification code failed too many times";
                return false;
            }
            detail = is_six_digit_code(code) ? "verification code mismatch" : "verification code is invalid";
            return false;
        }

        m_redis.del(code_key);
        m_redis.del(failed_key);
        detail.clear();
        return true;
    }

    std::unique_lock<std::mutex> lock(m_email_code_mutex);
    std::unordered_map<std::string, EmailVerificationCode>::iterator it = m_email_codes.find(make_email_code_key(phone, email));
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

    if (!is_six_digit_code(code) || it->second.code != code)
    {
        ++it->second.failed_attempts;
        if (it->second.failed_attempts >= kEmailCodeMaxFailedAttempts)
        {
            m_email_codes.erase(it);
            detail = "verification code failed too many times";
            return false;
        }
        detail = is_six_digit_code(code) ? "verification code mismatch" : "verification code is invalid";
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
    bool last_was_separator = false;
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(base[i]);
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.')
        {
            cleaned.push_back(static_cast<char>(ch));
            last_was_separator = false;
            continue;
        }

        if (ch < 128)
        {
            if (!last_was_separator && !cleaned.empty())
            {
                cleaned.push_back('_');
                last_was_separator = true;
            }
            continue;
        }

        std::size_t sequence_length = 0;
        if ((ch & 0xE0) == 0xC0)
        {
            sequence_length = 2;
        }
        else if ((ch & 0xF0) == 0xE0)
        {
            sequence_length = 3;
        }
        else if ((ch & 0xF8) == 0xF0)
        {
            sequence_length = 4;
        }

        bool valid_utf8 = sequence_length > 0 && i + sequence_length <= base.size();
        for (std::size_t j = 1; valid_utf8 && j < sequence_length; ++j)
        {
            const unsigned char next = static_cast<unsigned char>(base[i + j]);
            valid_utf8 = (next & 0xC0) == 0x80;
        }

        if (valid_utf8)
        {
            cleaned.append(base, i, sequence_length);
            i += sequence_length - 1;
            last_was_separator = false;
            continue;
        }

        if (!last_was_separator && !cleaned.empty())
        {
            cleaned.push_back('_');
            last_was_separator = true;
        }
    }

    while (!cleaned.empty() && (cleaned[0] == ' ' || cleaned[0] == '\t' || cleaned[0] == '.'))
    {
        cleaned.erase(0, 1);
    }

    while (!cleaned.empty() && (cleaned[cleaned.size() - 1] == ' ' ||
                                cleaned[cleaned.size() - 1] == '\t' ||
                                cleaned[cleaned.size() - 1] == '_'))
    {
        cleaned.erase(cleaned.size() - 1);
    }

    if (cleaned.empty() || cleaned == "." || cleaned == "..")
    {
        cleaned = "upload_image";
    }

    const std::size_t max_filename_bytes = 180;
    if (cleaned.size() > max_filename_bytes)
    {
        const std::size_t dot = cleaned.find_last_of('.');
        if (dot != std::string::npos && dot > 0 && cleaned.size() - dot <= 16)
        {
            const std::string ext = cleaned.substr(dot);
            cleaned = cleaned.substr(0, utf8_safe_prefix_length(cleaned, max_filename_bytes - ext.size())) + ext;
        }
        else
        {
            cleaned = cleaned.substr(0, utf8_safe_prefix_length(cleaned, max_filename_bytes));
        }
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

void WebServer::import_existing_images_to_db() const
{
    if (!m_db_pool.available())
    {
        return;
    }

    const std::string directory_path = join_path(m_root, "images");
    DIR *dir = opendir(directory_path.c_str());
    if (dir == nullptr)
    {
        return;
    }

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

        const std::string url = "/media/images/" + url_encode_path_segment(name);
        unsigned long long image_id = 0;
        if (!m_db_pool.insert_image(name,
                                    url,
                                    "legacy",
                                    static_cast<unsigned long long>(st.st_size),
                                    image_id))
        {
            AppLogger::error("existing image metadata import failed file=" + name + " detail=" + m_db_pool.last_error());
        }
    }

    closedir(dir);
}

bool WebServer::image_record_file_exists(const CGMysqlPool::ImageRecord &image) const
{
    if (image.filename.empty() ||
        image.filename.find('/') != std::string::npos ||
        image.filename.find('\\') != std::string::npos ||
        image.filename.find("..") != std::string::npos ||
        !is_image_extension(image.filename))
    {
        return false;
    }

    return file_exists(join_path(join_path(m_root, "images"), image.filename));
}

std::string WebServer::build_images_json(const std::string &username) const
{
    if (m_db_pool.available())
    {
        std::vector<CGMysqlPool::ImageRecord> db_entries;
        unsigned long long db_total = 0;
        if (m_db_pool.fetch_images_page(1, 1000, username, db_entries, db_total))
        {
            if (db_total == 0)
            {
                import_existing_images_to_db();
                db_entries.clear();
                m_db_pool.fetch_images_page(1, 1000, username, db_entries, db_total);
            }

            std::ostringstream db_body;
            db_body << "[";
            bool first = true;
            std::size_t visible_count = 0;
            for (std::size_t i = 0; i < db_entries.size(); ++i)
            {
                if (!image_record_file_exists(db_entries[i]))
                {
                    continue;
                }

                if (!first)
                {
                    db_body << ",";
                }
                first = false;
                ++visible_count;

                const std::string image_url = "/media/images/" + url_encode_path_segment(db_entries[i].filename);
                db_body << "{"
                        << "\"id\":" << db_entries[i].id << ","
                        << "\"title\":\"" << json_escape_string(db_entries[i].filename) << "\","
                        << "\"filename\":\"" << json_escape_string(db_entries[i].filename) << "\","
                        << "\"url\":\"" << json_escape_string(image_url) << "\","
                        << "\"path\":\"" << json_escape_string(image_url) << "\","
                        << "\"uploader\":\"" << json_escape_string(db_entries[i].uploader) << "\","
                        << "\"size\":" << db_entries[i].size << ","
                        << "\"like_count\":" << db_entries[i].like_count << ","
                        << "\"comment_count\":" << db_entries[i].comment_count << ","
                        << "\"favorite_count\":" << db_entries[i].favorite_count << ","
                        << "\"download_count\":" << db_entries[i].download_count << ","
                        << "\"liked\":" << (db_entries[i].liked ? "true" : "false") << ","
                        << "\"favorited\":" << (db_entries[i].favorited ? "true" : "false") << ","
                        << "\"created_at\":\"" << json_escape_string(db_entries[i].created_at) << "\""
                        << "}";
            }
            db_body << "]";
            if (visible_count > 0)
            {
                return db_body.str();
            }

            AppLogger::error("image list database page had no existing files, falling back to filesystem list");
        }

        AppLogger::error("image list database read failed: " + m_db_pool.last_error());
    }

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
        const std::string url = "/media/images/" + url_encode_path_segment(entries[i].name);
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

std::string WebServer::build_images_page_json(std::size_t page, std::size_t limit, const std::string &username) const
{
    if (m_db_pool.available())
    {
        std::vector<CGMysqlPool::ImageRecord> db_entries;
        unsigned long long db_total = 0;
        if (m_db_pool.fetch_images_page(page, limit, username, db_entries, db_total))
        {
            if (db_total == 0)
            {
                import_existing_images_to_db();
                db_entries.clear();
                m_db_pool.fetch_images_page(page, limit, username, db_entries, db_total);
            }

            const std::size_t end = (page > 0 && limit > 0)
                                        ? std::min<std::size_t>(static_cast<std::size_t>(db_total), page * limit)
                                        : 0;
            std::ostringstream db_body;
            db_body << "{\"ok\":true,"
                    << "\"page\":" << page << ","
                    << "\"limit\":" << limit << ","
                    << "\"total\":" << db_total << ","
                    << "\"has_more\":" << (end < db_total ? "true" : "false") << ","
                    << "\"items\":[";
            bool first = true;
            std::size_t visible_count = 0;
            for (std::size_t i = 0; i < db_entries.size(); ++i)
            {
                if (!image_record_file_exists(db_entries[i]))
                {
                    continue;
                }

                if (!first)
                {
                    db_body << ",";
                }
                first = false;
                ++visible_count;

                const std::string image_url = "/media/images/" + url_encode_path_segment(db_entries[i].filename);
                db_body << "{"
                        << "\"id\":" << db_entries[i].id << ","
                        << "\"title\":\"" << json_escape_string(db_entries[i].filename) << "\","
                        << "\"filename\":\"" << json_escape_string(db_entries[i].filename) << "\","
                        << "\"url\":\"" << json_escape_string(image_url) << "\","
                        << "\"path\":\"" << json_escape_string(image_url) << "\","
                        << "\"uploader\":\"" << json_escape_string(db_entries[i].uploader) << "\","
                        << "\"size\":" << db_entries[i].size << ","
                        << "\"like_count\":" << db_entries[i].like_count << ","
                        << "\"comment_count\":" << db_entries[i].comment_count << ","
                        << "\"favorite_count\":" << db_entries[i].favorite_count << ","
                        << "\"download_count\":" << db_entries[i].download_count << ","
                        << "\"liked\":" << (db_entries[i].liked ? "true" : "false") << ","
                        << "\"favorited\":" << (db_entries[i].favorited ? "true" : "false") << ","
                        << "\"created_at\":\"" << json_escape_string(db_entries[i].created_at) << "\""
                        << "}";
            }
            db_body << "]}";
            if (visible_count > 0 || db_total == 0)
            {
                return db_body.str();
            }

            AppLogger::error("image page database results had no existing files, falling back to filesystem list");
        }

        AppLogger::error("image list database read failed: " + m_db_pool.last_error());
    }

    struct ImageEntry
    {
        std::string name;
        time_t modified_at;
        unsigned long long size;
    };

    const std::string directory_path = join_path(m_root, "images");
    DIR *dir = opendir(directory_path.c_str());
    if (dir == nullptr)
    {
        return "{\"ok\":true,\"page\":1,\"limit\":0,\"total\":0,\"has_more\":false,\"items\":[]}";
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
        image_entry.size = static_cast<unsigned long long>(st.st_size);
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

    const std::size_t total = entries.size();
    const std::size_t offset = page > 0 ? (page - 1) * limit : 0;
    const std::size_t end = offset < total ? std::min(total, offset + limit) : offset;

    std::ostringstream body;
    body << "{\"ok\":true,"
         << "\"page\":" << page << ","
         << "\"limit\":" << limit << ","
         << "\"total\":" << total << ","
         << "\"has_more\":" << (end < total ? "true" : "false") << ","
         << "\"items\":[";
    for (std::size_t i = offset; i < end; ++i)
    {
        const std::string url = "/media/images/" + url_encode_path_segment(entries[i].name);
        body << "{"
             << "\"title\":\"" << json_escape_string(entries[i].name) << "\","
             << "\"url\":\"" << json_escape_string(url) << "\","
             << "\"path\":\"" << json_escape_string(url) << "\","
             << "\"size\":" << entries[i].size
             << "}";
        if (i + 1 < end)
        {
            body << ",";
        }
    }
    body << "]}";
    return body.str();
}

std::string WebServer::build_videos_json() const
{
    struct VideoEntry
    {
        std::string name;
        time_t modified_at;
        unsigned long long size;
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
        video_entry.size = static_cast<unsigned long long>(st.st_size);
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
        const std::string url = "/media/videos/" + url_encode_path_segment(entries[i].name);
        body << "{"
             << "\"title\":\"" << json_escape_string(entries[i].name) << "\","
             << "\"url\":\"" << json_escape_string(url) << "\","
             << "\"path\":\"" << json_escape_string(url) << "\","
             << "\"size\":" << entries[i].size
             << "}";
        if (i + 1 < entries.size())
        {
            body << ",";
        }
    }
    body << "]";
    return body.str();
}

std::string WebServer::build_cached_images_json() const
{
    std::string cached;
    if (m_redis.enabled() && m_redis.get(kRedisImagesListKey, cached))
    {
        return cached;
    }

    const std::string body = build_images_json("");
    if (m_redis.enabled() && !m_redis.setex(kRedisImagesListKey, kMediaListCacheTimeout, body))
    {
        AppLogger::error("image list cache write failed: " + m_redis.last_error());
    }
    return body;
}

std::string WebServer::build_cached_videos_json() const
{
    std::string cached;
    if (m_redis.enabled() && m_redis.get(kRedisVideosListKey, cached))
    {
        return cached;
    }

    const std::string body = build_videos_json();
    if (m_redis.enabled() && !m_redis.setex(kRedisVideosListKey, kMediaListCacheTimeout, body))
    {
        AppLogger::error("video list cache write failed: " + m_redis.last_error());
    }
    return body;
}

void WebServer::invalidate_media_list_cache(const std::string &directory_name) const
{
    if (!m_redis.enabled())
    {
        return;
    }

    const char *key = directory_name == "videos" ? kRedisVideosListKey : kRedisImagesListKey;
    if (!m_redis.del(key))
    {
        AppLogger::error("media list cache invalidation failed: " + m_redis.last_error());
    }
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
    const std::size_t max_upload_size = expect_image ? m_max_image_upload_size : m_max_video_upload_size;
    const std::string max_upload_label = format_byte_limit(max_upload_size);

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
    errno = 0;
    std::ofstream output(full_path.c_str(), std::ios::out | std::ios::binary);
    if (!output.is_open())
    {
        detail = std::string(expect_image ? "failed to open target image file: "
                                          : "failed to open target video file: ") +
                 full_path + " errno=" + std::to_string(errno);
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

    saved_path = "/media/" + directory_name + "/" + url_encode_path_segment(final_name);
    invalidate_media_list_cache(directory_name);
    detail.clear();
    return true;
}

bool WebServer::save_uploaded_video_chunk(const HttpConn::Request &request, std::string &saved_path, std::string &detail) const
{
    const std::string upload_id = sanitize_upload_filename(get_header_value(request, "X-Upload-Id"));
    const std::string filename = sanitize_upload_filename(m_http_conn.url_decode(get_header_value(request, "X-File-Name")));
    const std::string chunk_index_text = get_header_value(request, "X-Chunk-Index");
    const std::string total_chunks_text = get_header_value(request, "X-Total-Chunks");
    const std::string file_size_text = get_header_value(request, "X-File-Size");

    unsigned long long chunk_index = 0;
    unsigned long long total_chunks = 0;
    unsigned long long file_size = 0;

    if (upload_id.empty() || filename.empty() ||
        !parse_unsigned_number(chunk_index_text, chunk_index) ||
        !parse_unsigned_number(total_chunks_text, total_chunks) ||
        !parse_unsigned_number(file_size_text, file_size))
    {
        detail = "missing chunk upload metadata";
        return false;
    }

    if (!is_safe_video_upload_extension(filename))
    {
        detail = "only mp4, webm, ogg, mov, avi, mkv, m4v are supported";
        return false;
    }

    if (total_chunks == 0 || chunk_index >= total_chunks)
    {
        detail = "invalid chunk index";
        return false;
    }

    if (file_size == 0 || file_size > m_max_video_upload_size)
    {
        detail = "video is too large, max size is " + format_byte_limit(m_max_video_upload_size);
        return false;
    }

    if (request.body.empty() || request.body.size() > kMaxVideoChunkBodySize)
    {
        detail = "invalid video chunk size";
        return false;
    }

    if (chunk_index + 1 < total_chunks && request.body.size() != kVideoChunkSize)
    {
        detail = "non-final video chunk size must be 2MB";
        return false;
    }

    const std::string directory_path = join_path(m_root, "videos");
    if (!ensure_directory_exists(directory_path))
    {
        detail = "failed to create videos directory";
        return false;
    }

    const std::string temp_name = upload_id + ".part";
    const std::string temp_path = join_path(directory_path, temp_name);

    if (chunk_index == 0)
    {
        std::ofstream reset(temp_path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!reset.is_open())
        {
            detail = "failed to create temporary video upload file";
            return false;
        }
    }

    struct stat st;
    std::memset(&st, 0, sizeof(st));
    const unsigned long long expected_offset = chunk_index * static_cast<unsigned long long>(kVideoChunkSize);
    const bool temp_exists = stat(temp_path.c_str(), &st) == 0;
    const unsigned long long current_size = temp_exists ? static_cast<unsigned long long>(st.st_size) : 0;
    if (current_size != expected_offset)
    {
        detail = "video chunks must be uploaded in order";
        return false;
    }

    errno = 0;
    std::ofstream output(temp_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
    if (!output.is_open())
    {
        detail = "failed to open temporary video upload file errno=" + std::to_string(errno);
        return false;
    }
    output.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
    output.close();
    if (!output)
    {
        detail = "failed to write video chunk";
        return false;
    }

    if (chunk_index + 1 < total_chunks)
    {
        saved_path.clear();
        detail.clear();
        return true;
    }

    std::memset(&st, 0, sizeof(st));
    if (stat(temp_path.c_str(), &st) != 0 || static_cast<unsigned long long>(st.st_size) != file_size)
    {
        detail = "merged video size does not match upload metadata";
        return false;
    }

    const std::string final_name = unique_upload_filename("videos", filename);
    const std::string final_path = join_path(directory_path, final_name);
    if (std::rename(temp_path.c_str(), final_path.c_str()) != 0)
    {
        detail = "failed to finalize uploaded video";
        return false;
    }

    saved_path = "/media/videos/" + url_encode_path_segment(final_name);
    invalidate_media_list_cache("videos");
    detail.clear();
    return true;
}

HttpConn::Response WebServer::handle_upload_api(const HttpConn::Request &request) const
{
    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin) || !is_admin)
    {
        return build_response_with_body(403,
                                        "Forbidden",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"only admin can upload images\"}");
    }

    std::string saved_path;
    std::string detail;
    if (!save_uploaded_image(request, saved_path, detail))
    {
        AppLogger::error("image upload failed: " + detail);
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(detail) + "\"}");
    }

    unsigned long long image_id = 0;
    if (m_db_pool.available())
    {
        const std::size_t slash = saved_path.find_last_of('/');
        const std::string encoded_filename = slash == std::string::npos ? saved_path : saved_path.substr(slash + 1);
        const std::string filename = m_http_conn.url_decode(encoded_filename);
        const std::string full_path = join_path(join_path(m_root, "images"), filename);
        unsigned long long file_size = 0;
        struct stat st;
        std::memset(&st, 0, sizeof(st));
        if (stat(full_path.c_str(), &st) == 0)
        {
            file_size = static_cast<unsigned long long>(st.st_size);
        }

        if (!m_db_pool.insert_image(filename, saved_path, username, file_size, image_id))
        {
            AppLogger::error("image metadata insert failed path=" + saved_path + " detail=" + m_db_pool.last_error());
        }
    }

    AppLogger::info("image upload success path=" + saved_path);
    std::ostringstream body;
    body << "{"
         << "\"ok\":true,"
         << "\"message\":\"upload success\","
         << "\"path\":\"" << json_escape_string(saved_path) << "\","
         << "\"id\":" << image_id
         << "}";
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    body.str());
}

HttpConn::Response WebServer::handle_upload_video_api(const HttpConn::Request &request) const
{
    if (!current_user_is_admin(request))
    {
        return build_response_with_body(403,
                                        "Forbidden",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"only admin can upload videos\"}");
    }

    std::string saved_path;
    std::string detail;
    if (!save_uploaded_media(request, "videos", "video", false, saved_path, detail))
    {
        AppLogger::error("video upload failed: " + detail);
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(detail) + "\"}");
    }

    AppLogger::info("video upload success path=" + saved_path);
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    std::string("{\"ok\":true,\"message\":\"upload success\",\"path\":\"") +
                                        json_escape_string(saved_path) + "\"}");
}

HttpConn::Response WebServer::handle_upload_video_chunk_api(const HttpConn::Request &request) const
{
    if (!current_user_is_admin(request))
    {
        return build_response_with_body(403,
                                        "Forbidden",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"only admin can upload videos\"}");
    }

    std::string saved_path;
    std::string detail;
    if (!save_uploaded_video_chunk(request, saved_path, detail))
    {
        AppLogger::error("video chunk upload failed: " + detail);
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(detail) + "\"}");
    }

    if (saved_path.empty())
    {
        return build_response_with_body(200,
                                        "OK",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":true,\"complete\":false}");
    }

    AppLogger::info("video chunk upload complete path=" + saved_path);
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    std::string("{\"ok\":true,\"complete\":true,\"path\":\"") +
                                        json_escape_string(saved_path) + "\"}");
}

HttpConn::Response WebServer::handle_list_images_api(const HttpConn::Request &request) const
{
    std::string username;
    bool is_admin = false;
    get_session(request, username, is_admin);

    if (has_query_param(request.raw_target, "page") || has_query_param(request.raw_target, "limit"))
    {
        const std::size_t page = query_param_size(request.raw_target, "page", 1, 1, 1000000);
        const std::size_t limit = query_param_size(request.raw_target,
                                                  "limit",
                                                  kDefaultImagePageSize,
                                                  1,
                                                  kMaxImagePageSize);
        return build_response_with_body(200,
                                        "OK",
                                        "application/json; charset=utf-8",
                                        build_images_page_json(page, limit, username));
    }

    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    build_images_json(username));
}

HttpConn::Response WebServer::handle_my_favorites_api(const HttpConn::Request &request) const
{
    if (request.method != "GET")
    {
        return build_response_with_body(405,
                                        "Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"method not allowed\"}");
    }

    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_unauthorized_json_response();
    }

    if (!m_db_pool.available())
    {
        return build_response_with_body(503,
                                        "Service Unavailable",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"database is unavailable\"}");
    }

    const std::size_t page = query_param_size(request.raw_target, "page", 1, 1, 1000000);
    const std::size_t limit = query_param_size(request.raw_target,
                                               "limit",
                                               kDefaultImagePageSize,
                                               1,
                                               kMaxImagePageSize);
    std::vector<CGMysqlPool::FavoriteImageRecord> favorites;
    unsigned long long total = 0;
    if (!m_db_pool.fetch_user_favorites(username, page, limit, favorites, total))
    {
        AppLogger::error("favorites read failed username=" + username + " detail=" + m_db_pool.last_error());
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"favorites read failed\"}");
    }

    const std::size_t end = page > 0 && limit > 0
                                ? std::min<std::size_t>(static_cast<std::size_t>(total), page * limit)
                                : 0;
    std::ostringstream body;
    body << "{\"ok\":true,"
         << "\"page\":" << page << ","
         << "\"limit\":" << limit << ","
         << "\"total\":" << total << ","
         << "\"has_more\":" << (end < total ? "true" : "false") << ","
         << "\"items\":[";
    bool first = true;
    for (std::size_t i = 0; i < favorites.size(); ++i)
    {
        if (!image_record_file_exists(favorites[i].image))
        {
            continue;
        }

        if (!first)
        {
            body << ",";
        }
        first = false;

        const std::string image_url = "/media/images/" + url_encode_path_segment(favorites[i].image.filename);
        body << "{"
             << "\"id\":" << favorites[i].image.id << ","
             << "\"title\":\"" << json_escape_string(favorites[i].image.filename) << "\","
             << "\"filename\":\"" << json_escape_string(favorites[i].image.filename) << "\","
             << "\"url\":\"" << json_escape_string(image_url) << "\","
             << "\"path\":\"" << json_escape_string(image_url) << "\","
             << "\"uploader\":\"" << json_escape_string(favorites[i].image.uploader) << "\","
             << "\"size\":" << favorites[i].image.size << ","
             << "\"like_count\":" << favorites[i].image.like_count << ","
             << "\"comment_count\":" << favorites[i].image.comment_count << ","
             << "\"favorite_count\":" << favorites[i].image.favorite_count << ","
             << "\"download_count\":" << favorites[i].image.download_count << ","
             << "\"liked\":" << (favorites[i].image.liked ? "true" : "false") << ","
             << "\"favorited\":true,"
             << "\"created_at\":\"" << json_escape_string(favorites[i].image.created_at) << "\","
             << "\"favorited_at\":\"" << json_escape_string(favorites[i].favorited_at) << "\""
             << "}";
    }
    body << "]}";
    return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
}

HttpConn::Response WebServer::handle_image_reaction_api(const HttpConn::Request &request) const
{
    unsigned long long image_id = 0;
    std::string action;
    if (!parse_image_reaction_path(request.path, image_id, action))
    {
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"image action was not found\"}");
    }

    if (request.method != "POST" && request.method != "DELETE")
    {
        return build_response_with_body(405,
                                        "Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"method not allowed\"}");
    }

    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_unauthorized_json_response();
    }

    if (!m_db_pool.available())
    {
        return build_response_with_body(503,
                                        "Service Unavailable",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"database is unavailable\"}");
    }

    CGMysqlPool::ImageReactionState state;
    const bool active = request.method == "POST";
    const bool ok = action == "like"
                        ? m_db_pool.set_image_like(image_id, username, active, state)
                        : m_db_pool.set_image_favorite(image_id, username, active, state);
    if (!ok)
    {
        const std::string detail = m_db_pool.last_error();
        const bool not_found = detail == "Image was not found.";
        return build_response_with_body(not_found ? 404 : 500,
                                        not_found ? "Not Found" : "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(not_found ? "image was not found" : "image action failed") + "\"}");
    }

    std::ostringstream body;
    body << "{"
         << "\"ok\":true,"
         << "\"id\":" << state.image_id << ","
         << "\"liked\":" << (state.liked ? "true" : "false") << ","
         << "\"favorited\":" << (state.favorited ? "true" : "false") << ","
         << "\"like_count\":" << state.like_count << ","
         << "\"favorite_count\":" << state.favorite_count
         << "}";
    return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
}

HttpConn::Response WebServer::handle_list_image_comments_api(const HttpConn::Request &request) const
{
    if (request.method != "GET")
    {
        return build_response_with_body(405,
                                        "Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"method not allowed\"}");
    }

    unsigned long long image_id = 0;
    if (!parse_image_comments_path(request.path, image_id))
    {
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"comments endpoint was not found\"}");
    }

    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_unauthorized_json_response();
    }

    if (!m_db_pool.available())
    {
        return build_response_with_body(503,
                                        "Service Unavailable",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"database is unavailable\"}");
    }

    const std::size_t page = query_param_size(request.raw_target, "page", 1, 1, 1000000);
    const std::size_t limit = query_param_size(request.raw_target,
                                               "limit",
                                               kDefaultCommentPageSize,
                                               1,
                                               kMaxCommentPageSize);
    std::vector<CGMysqlPool::ImageCommentRecord> comments;
    unsigned long long total = 0;
    if (!m_db_pool.fetch_image_comments(image_id, page, limit, comments, total))
    {
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"comments read failed\"}");
    }

    const std::size_t end = page > 0 && limit > 0
                                ? std::min<std::size_t>(static_cast<std::size_t>(total), page * limit)
                                : 0;
    std::ostringstream body;
    body << "{\"ok\":true,"
         << "\"page\":" << page << ","
         << "\"limit\":" << limit << ","
         << "\"total\":" << total << ","
         << "\"has_more\":" << (end < total ? "true" : "false") << ","
         << "\"items\":[";
    for (std::size_t i = 0; i < comments.size(); ++i)
    {
        if (i > 0)
        {
            body << ",";
        }
        body << "{"
             << "\"id\":" << comments[i].id << ","
             << "\"image_id\":" << comments[i].image_id << ","
             << "\"username\":\"" << json_escape_string(comments[i].username) << "\","
             << "\"content\":\"" << json_escape_string(comments[i].content) << "\","
             << "\"created_at\":\"" << json_escape_string(comments[i].created_at) << "\","
             << "\"is_owner\":" << (comments[i].username == username || is_admin ? "true" : "false")
             << "}";
    }
    body << "]}";
    return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
}

HttpConn::Response WebServer::handle_create_image_comment_api(const HttpConn::Request &request) const
{
    if (request.method != "POST")
    {
        return build_response_with_body(405,
                                        "Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"method not allowed\"}");
    }

    unsigned long long image_id = 0;
    if (!parse_image_comments_path(request.path, image_id))
    {
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"comments endpoint was not found\"}");
    }

    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_unauthorized_json_response();
    }

    std::map<std::string, std::string> fields = m_http_conn.parse_form_urlencoded(request.body);
    const std::string content = trim_whitespace(fields["content"]);
    if (content.empty())
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"comment content is required\"}");
    }
    if (utf8_codepoint_count(content) > kMaxCommentLength)
    {
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"comment content is too long\"}");
    }

    if (!m_db_pool.available())
    {
        return build_response_with_body(503,
                                        "Service Unavailable",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"database is unavailable\"}");
    }

    CGMysqlPool::ImageCommentRecord comment;
    if (!m_db_pool.insert_image_comment(image_id, username, content, comment))
    {
        const std::string detail = m_db_pool.last_error();
        const bool not_found = detail == "Image was not found.";
        return build_response_with_body(not_found ? 404 : 500,
                                        not_found ? "Not Found" : "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(not_found ? "image was not found" : "comment create failed") + "\"}");
    }

    invalidate_media_list_cache("images");

    std::ostringstream body;
    body << "{\"ok\":true,"
         << "\"comment\":{"
         << "\"id\":" << comment.id << ","
         << "\"image_id\":" << comment.image_id << ","
         << "\"username\":\"" << json_escape_string(comment.username) << "\","
         << "\"content\":\"" << json_escape_string(comment.content) << "\","
         << "\"created_at\":\"" << json_escape_string(comment.created_at) << "\","
         << "\"is_owner\":true"
         << "}}";
    return build_response_with_body(201, "Created", "application/json; charset=utf-8", body.str());
}

HttpConn::Response WebServer::handle_delete_image_comment_api(const HttpConn::Request &request) const
{
    if (request.method != "DELETE")
    {
        return build_response_with_body(405,
                                        "Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"method not allowed\"}");
    }

    unsigned long long comment_id = 0;
    if (!parse_image_comment_path(request.path, comment_id))
    {
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"comment endpoint was not found\"}");
    }

    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_unauthorized_json_response();
    }

    if (!m_db_pool.available())
    {
        return build_response_with_body(503,
                                        "Service Unavailable",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"database is unavailable\"}");
    }

    unsigned long long image_id = 0;
    if (!m_db_pool.soft_delete_image_comment(comment_id, username, is_admin, image_id))
    {
        const std::string detail = m_db_pool.last_error();
        const bool not_found = detail == "Comment was not found.";
        const bool forbidden = detail == "Comment delete is forbidden.";
        return build_response_with_body(forbidden ? 403 : (not_found ? 404 : 500),
                                        forbidden ? "Forbidden" : (not_found ? "Not Found" : "Internal Server Error"),
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(forbidden ? "comment delete is forbidden" : (not_found ? "comment was not found" : "comment delete failed")) + "\"}");
    }

    invalidate_media_list_cache("images");
    std::ostringstream body;
    body << "{\"ok\":true,\"id\":" << comment_id << ",\"image_id\":" << image_id << "}";
    return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
}

HttpConn::Response WebServer::handle_image_download_api(const HttpConn::Request &request) const
{
    if (request.method != "GET")
    {
        return build_response_with_body(405,
                                        "Method Not Allowed",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"method not allowed\"}");
    }

    unsigned long long image_id = 0;
    if (!parse_image_download_path(request.path, image_id))
    {
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"image download was not found\"}");
    }

    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_unauthorized_json_response();
    }

    if (!m_db_pool.available())
    {
        return build_response_with_body(503,
                                        "Service Unavailable",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"database is unavailable\"}");
    }

    CGMysqlPool::ImageRecord image;
    if (!m_db_pool.fetch_image_by_id(image_id, image))
    {
        const std::string detail = m_db_pool.last_error();
        const bool not_found = detail == "Image was not found.";
        return build_response_with_body(not_found ? 404 : 500,
                                        not_found ? "Not Found" : "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape_string(not_found ? "image was not found" : "image download failed") + "\"}");
    }

    if (image.filename.empty() ||
        image.filename.find('/') != std::string::npos ||
        image.filename.find('\\') != std::string::npos ||
        image.filename.find("..") != std::string::npos ||
        !is_image_extension(image.filename))
    {
        return build_response_with_body(403,
                                        "Forbidden",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"invalid image filename\"}");
    }

    const std::string full_path = join_path(join_path(m_root, "images"), image.filename);
    if (!file_exists(full_path))
    {
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"image file was not found\"}");
    }

    unsigned long long download_count = 0;
    if (!m_db_pool.increment_image_download_count(image_id, download_count))
    {
        AppLogger::error("image download count update failed id=" + std::to_string(image_id) +
                         " detail=" + m_db_pool.last_error());
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"image download failed\"}");
    }

    const std::string encoded_filename = url_encode_path_segment(image.filename);
    const std::string disposition = attachment_disposition_header(image.filename, encoded_filename);
    if (get_header_value(request, "X-Accel-Enabled") == "1")
    {
        HttpConn::Response response = build_response_with_body(200, "OK", get_content_type(image.filename), "");
        response.headers["X-Accel-Redirect"] = "/_protected_images/" + encoded_filename;
        response.headers["Content-Disposition"] = disposition;
        response.headers["Cache-Control"] = "private";
        response.headers["X-Image-Download-Count"] = std::to_string(download_count);
        return response;
    }

    HttpConn::Response response = build_static_file_response(request, full_path);
    response.headers["Content-Disposition"] = disposition;
    response.headers["Cache-Control"] = "private";
    response.headers["X-Image-Download-Count"] = std::to_string(download_count);
    return response;
}

HttpConn::Response WebServer::handle_list_videos_api() const
{
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    build_cached_videos_json());
}

HttpConn::Response WebServer::handle_media_api(const HttpConn::Request &request) const
{
    const std::string image_prefix = "/media/images/";
    const std::string video_prefix = "/media/videos/";
    std::string directory_name;
    std::string accel_prefix;
    std::string filename;
    std::string raw_filename;
    std::string raw_path = request.raw_target;
    const std::size_t raw_query_pos = raw_path.find_first_of("?#");
    if (raw_query_pos != std::string::npos)
    {
        raw_path = raw_path.substr(0, raw_query_pos);
    }

    if (request.path.find(image_prefix) == 0)
    {
        directory_name = "images";
        accel_prefix = "/_protected_images/";
        filename = request.path.substr(image_prefix.size());
        if (raw_path.find(image_prefix) == 0)
        {
            raw_filename = raw_path.substr(image_prefix.size());
        }
    }
    else if (request.path.find(video_prefix) == 0)
    {
        directory_name = "videos";
        accel_prefix = "/_protected_videos/";
        filename = request.path.substr(video_prefix.size());
        if (raw_path.find(video_prefix) == 0)
        {
            raw_filename = raw_path.substr(video_prefix.size());
        }
    }
    else
    {
        return build_error_response(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
    }

    if (filename.empty() ||
        filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos)
    {
        return build_error_response(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
    }

    std::string resolved_filename = filename;
    std::string full_path = join_path(join_path(m_root, directory_name), resolved_filename);
    bool ext_ok = directory_name == "images" ? is_image_extension(resolved_filename)
                                             : is_video_extension(resolved_filename);
    if ((!ext_ok || !file_exists(full_path)) && filename.find('%') != std::string::npos)
    {
        const std::string decoded_filename = m_http_conn.url_decode(filename);
        if (!decoded_filename.empty() &&
            decoded_filename.find('/') == std::string::npos &&
            decoded_filename.find('\\') == std::string::npos &&
            decoded_filename.find("..") == std::string::npos)
        {
            const std::string decoded_path = join_path(join_path(m_root, directory_name), decoded_filename);
            const bool decoded_ext_ok = directory_name == "images" ? is_image_extension(decoded_filename)
                                                                   : is_video_extension(decoded_filename);
            if (decoded_ext_ok && file_exists(decoded_path))
            {
                resolved_filename = decoded_filename;
                full_path = decoded_path;
                ext_ok = true;
            }
        }
    }

    if (!ext_ok || !file_exists(full_path))
    {
        const std::string directory_path = join_path(m_root, directory_name);
        DIR *dir = opendir(directory_path.c_str());
        if (dir != nullptr)
        {
            dirent *entry = nullptr;
            while ((entry = readdir(dir)) != nullptr)
            {
                const std::string candidate = entry->d_name;
                if (candidate == "." || candidate == ".." ||
                    candidate.find('/') != std::string::npos ||
                    candidate.find('\\') != std::string::npos ||
                    candidate.find("..") != std::string::npos)
                {
                    continue;
                }

                const bool candidate_ext_ok = directory_name == "images" ? is_image_extension(candidate)
                                                                         : is_video_extension(candidate);
                if (!candidate_ext_ok)
                {
                    continue;
                }

                const std::string candidate_path = join_path(directory_path, candidate);
                if (!file_exists(candidate_path))
                {
                    continue;
                }

                const std::string encoded_candidate = url_encode_path_segment(candidate);
                if (candidate == filename ||
                    candidate == raw_filename ||
                    encoded_candidate == filename ||
                    encoded_candidate == raw_filename)
                {
                    resolved_filename = candidate;
                    full_path = candidate_path;
                    ext_ok = true;
                    break;
                }
            }

            closedir(dir);
        }
    }

    if (!ext_ok || !file_exists(full_path))
    {
        return build_error_response(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
    }

    if (get_header_value(request, "X-Accel-Enabled") != "1")
    {
        return build_static_file_response(request, full_path);
    }

    HttpConn::Response response = build_response_with_body(200, "OK", get_content_type(resolved_filename), "");
    response.headers["X-Accel-Redirect"] = accel_prefix + url_encode_path_segment(resolved_filename);
    response.headers["Cache-Control"] = "private";
    return response;
}

HttpConn::Response WebServer::handle_current_user_api(const HttpConn::Request &request) const
{
    std::string username;
    bool is_admin = false;
    if (!get_session(request, username, is_admin))
    {
        return build_response_with_body(401,
                                        "Unauthorized",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"not logged in\",\"redirect\":\"/login.html\"}");
    }

    std::ostringstream body;
    body << "{"
         << "\"ok\":true,"
         << "\"username\":\"" << json_escape(username) << "\","
         << "\"role\":\"" << (is_admin ? "admin" : "user") << "\""
         << "}";
    return build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
}

HttpConn::Response WebServer::handle_logout_api(const HttpConn::Request &request) const
{
    destroy_session(request);

    HttpConn::Response response = build_response_with_body(200,
                                                           "OK",
                                                           "application/json; charset=utf-8",
                                                           "{\"ok\":true,\"message\":\"logout success\",\"redirect\":\"/login.html\"}");
    response.headers["Set-Cookie"] = std::string(kSessionCookieName) + "=; HttpOnly; Path=/; SameSite=Lax; Max-Age=0";
    return response;
}

HttpConn::Response WebServer::handle_login_api(const HttpConn::Request &request) const
{
    const std::map<std::string, std::string> form = m_http_conn.parse_form_urlencoded(request.body);
    const std::string username = form.count("username") ? form.find("username")->second : "";
    const std::string password = form.count("password") ? form.find("password")->second : "";
    const bool remember = form.count("remember") && form.find("remember")->second == "on";

    if (username == "admin" && password == "12345")
    {
        const std::string token = create_session(username, true, remember);
        AppLogger::info("login success username=admin role=admin source=fallback");
        std::ostringstream body;
        body << "{"
             << "\"ok\":true,"
             << "\"message\":\"admin login success\","
             << "\"redirect\":\"/home.html\","
             << "\"remember\":" << (remember ? "true" : "false") << ","
             << "\"username\":\"" << json_escape(username) << "\","
             << "\"role\":\"admin\","
             << "\"source\":\"fallback\""
             << "}";
        HttpConn::Response response = build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
        response.headers["Set-Cookie"] = session_cookie_header(token, remember);
        return response;
    }

    std::string db_detail;
    if (validate_user_with_db(username, password, db_detail))
    {
        const std::string token = create_session(username, false, remember);
        AppLogger::info("login success username=" + username + " role=user source=mysql");
        std::ostringstream body;
        body << "{"
             << "\"ok\":true,"
             << "\"message\":\"database login success\","
             << "\"redirect\":\"/home.html\","
             << "\"remember\":" << (remember ? "true" : "false") << ","
             << "\"username\":\"" << json_escape(username) << "\","
             << "\"role\":\"user\","
             << "\"source\":\"mysql\""
             << "}";
        HttpConn::Response response = build_response_with_body(200, "OK", "application/json; charset=utf-8", body.str());
        response.headers["Set-Cookie"] = session_cookie_header(token, remember);
        return response;
    }

    if (!db_detail.empty() && db_detail != "password mismatch")
    {
        AppLogger::error("login failed username=" + username + " detail=" + db_detail);
    }
    else
    {
        AppLogger::info("login failed username=" + username + " reason=invalid_credentials");
    }

    const std::string message = "username or password is invalid";
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

    if (!verify_email_code(phone, email, email_code, detail))
    {
        AppLogger::info("register failed phone=" + phone + " email=" + AppLogger::mask_email(email) + " reason=" + detail);
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") + json_escape(detail) + "\"}");
    }

    if (!register_user_with_db(phone, password, detail))
    {
        AppLogger::error("register failed phone=" + phone + " email=" + AppLogger::mask_email(email) + " detail=" + detail);
        const int status = detail == "username already exists" ? 409 : 500;
        const std::string status_text = detail == "username already exists" ? "Conflict" : "Internal Server Error";
        if (detail == "username already exists")
        {
            detail = "phone number already registered";
        }
        else
        {
            detail = "registration failed";
        }
        return build_response_with_body(status,
                                        status_text,
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") + json_escape(detail) + "\"}");
    }

    AppLogger::info("register success phone=" + phone + " email=" + AppLogger::mask_email(email));
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
    const std::string phone = form.count("phone") ? form.find("phone")->second : "";
    const std::string email = form.count("email") ? form.find("email")->second : "";

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

    cleanup_expired_email_state(time(nullptr));

    std::string detail;
    if (!consume_email_code_rate_limit(phone, request.client_ip, detail))
    {
        AppLogger::info("verification code limited phone=" + phone + " ip=" + request.client_ip + " reason=" + detail);
        return build_response_with_body(429,
                                        "Too Many Requests",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") + json_escape(detail) + "\"}");
    }

    const std::string code = generate_email_verification_code();
    if (!SmtpClient::send_verification_code(email, code, detail))
    {
        AppLogger::error("SMTP send failed email=" + AppLogger::mask_email(email) + " detail=" + detail);
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"verification code email failed\"}");
    }

    if (!save_email_verification_code(phone, email, code, detail))
    {
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"verification code storage failed\"}");
    }

    AppLogger::info("verification code sent phone=" + phone + " email=" + AppLogger::mask_email(email) + " ip=" + request.client_ip);

    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    "{\"ok\":true,\"message\":\"verification code sent\"}");
}

HttpConn::Response WebServer::handle_reset_api(const HttpConn::Request &request) const
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

    if (!m_db_pool.available())
    {
        detail = m_db_pool.last_error();
        AppLogger::error("reset failed phone=" + phone + " detail=" + detail);
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"password reset failed\"}");
    }

    bool exists = false;
    if (!m_db_pool.user_exists(phone, exists))
    {
        detail = m_db_pool.last_error();
        AppLogger::error("reset failed phone=" + phone + " detail=" + detail);
        return build_response_with_body(500,
                                        "Internal Server Error",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"password reset failed\"}");
    }

    if (!exists)
    {
        AppLogger::info("reset failed phone=" + phone + " reason=not_registered");
        return build_response_with_body(404,
                                        "Not Found",
                                        "application/json; charset=utf-8",
                                        "{\"ok\":false,\"message\":\"phone number was not registered\"}");
    }

    if (!verify_email_code(phone, email, email_code, detail))
    {
        AppLogger::info("reset failed phone=" + phone + " email=" + AppLogger::mask_email(email) + " reason=" + detail);
        return build_response_with_body(400,
                                        "Bad Request",
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") + json_escape(detail) + "\"}");
    }

    if (!reset_user_password_with_db(phone, password, detail))
    {
        AppLogger::error("reset failed phone=" + phone + " email=" + AppLogger::mask_email(email) + " detail=" + detail);
        const int status = detail == "phone number was not registered" ? 404 : 500;
        const std::string status_text = detail == "phone number was not registered" ? "Not Found" : "Internal Server Error";
        return build_response_with_body(status,
                                        status_text,
                                        "application/json; charset=utf-8",
                                        std::string("{\"ok\":false,\"message\":\"") +
                                            json_escape(status == 404 ? detail : "password reset failed") + "\"}");
    }

    AppLogger::info("reset success phone=" + phone + " email=" + AppLogger::mask_email(email));
    return build_response_with_body(200,
                                    "OK",
                                    "application/json; charset=utf-8",
                                    "{\"ok\":true,\"message\":\"password reset success, please log in with your new password\"}");
}

HttpConn::Response WebServer::handle_request(const HttpConn::Request &request) const
{
    if (!is_safe_path(request.path))
    {
        return build_error_response(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
    }

    if (is_protected_api(request) || is_protected_page_path(request.path))
    {
        std::string username;
        bool is_admin = false;
        if (!get_session(request, username, is_admin))
        {
            return is_protected_api(request) ? build_unauthorized_json_response()
                                             : build_login_redirect_response();
        }
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
    if (request.method == "GET" && request.path == "/api/me")
    {
        return handle_current_user_api(request);
    }
    if (request.method == "GET" && request.path == "/api/me/favorites")
    {
        return handle_my_favorites_api(request);
    }
    if (request.method == "POST" && request.path == "/api/logout")
    {
        return handle_logout_api(request);
    }
    if (request.method == "POST" && request.path == "/api/upload")
    {
        return handle_upload_api(request);
    }
    if (request.method == "POST" && request.path == "/api/upload-video")
    {
        return handle_upload_video_api(request);
    }
    if (request.method == "POST" && request.path == "/api/upload-video-chunk")
    {
        return handle_upload_video_chunk_api(request);
    }
    if (request.method == "GET" && request.path == "/api/images")
    {
        return handle_list_images_api(request);
    }
    unsigned long long image_comments_id = 0;
    if (parse_image_comments_path(request.path, image_comments_id))
    {
        return request.method == "GET" ? handle_list_image_comments_api(request)
                                       : handle_create_image_comment_api(request);
    }
    unsigned long long comment_id = 0;
    if (parse_image_comment_path(request.path, comment_id))
    {
        return handle_delete_image_comment_api(request);
    }
    unsigned long long image_download_id = 0;
    if (parse_image_download_path(request.path, image_download_id))
    {
        return handle_image_download_api(request);
    }
    unsigned long long image_action_id = 0;
    std::string image_action;
    if (parse_image_reaction_path(request.path, image_action_id, image_action))
    {
        return handle_image_reaction_api(request);
    }
    if (request.method == "GET" && request.path == "/api/videos")
    {
        return handle_list_videos_api();
    }
    if ((request.method == "GET" || request.method == "HEAD") &&
        (request.path.find("/media/images/") == 0 || request.path.find("/media/videos/") == 0))
    {
        return handle_media_api(request);
    }

    if (request.method != "GET")
    {
        return build_error_response(405, "Method Not Allowed", "<html><body><h1>405 Method Not Allowed</h1></body></html>");
    }

    std::string effective_path = request.path;
    if (effective_path == "/")
    {
        const std::string home_path = m_root + "/home.html";
        const std::string login_path = m_root + "/login.html";
        const std::string index_path = m_root + "/index.html";
        if (file_exists(home_path))
        {
            effective_path = "/home.html";
        }
        else if (file_exists(login_path))
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
        return build_error_response(403, "Forbidden", "<html><body><h1>403 Forbidden</h1></body></html>");
    }

    if (!file_exists(full_path))
    {
        return build_error_response(404, "Not Found", "<html><body><h1>404 Not Found</h1></body></html>");
    }

    return build_static_file_response(request, full_path);
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
    request.client_ip = get_client_ip(sockfd);

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
