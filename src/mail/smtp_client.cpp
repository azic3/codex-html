#include "smtp_client.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#if defined(__has_include)
#if __has_include(<curl/curl.h>)
#define XIAOCHEN_HAS_CURL 1
#include <curl/curl.h>
#else
#define XIAOCHEN_HAS_CURL 0
#endif
#else
#define XIAOCHEN_HAS_CURL 0
#endif

namespace
{
std::string getenv_string(const char *primary, const char *fallback = nullptr)
{
    const char *value = std::getenv(primary);
    if ((!value || !*value) && fallback)
    {
        value = std::getenv(fallback);
    }
    return value ? value : "";
}

bool env_truthy(const char *name)
{
    const std::string value = getenv_string(name);
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

std::string build_message(const std::string &from,
                          const std::string &from_name,
                          const std::string &to,
                          const std::string &code)
{
    std::ostringstream message;
    message << "From: ";
    if (!from_name.empty())
    {
        message << from_name << " ";
    }
    message << "<" << from << ">\r\n";
    message << "To: <" << to << ">\r\n";
    message << "Subject: XIAOCHEN verification code\r\n";
    message << "MIME-Version: 1.0\r\n";
    message << "Content-Type: text/plain; charset=UTF-8\r\n";
    message << "Content-Transfer-Encoding: 8bit\r\n";
    message << "\r\n";
    message << "Your XIAOCHEN verification code is: " << code << "\r\n";
    message << "This code expires in 5 minutes. If you did not request it, please ignore this email.\r\n";
    return message.str();
}

#if XIAOCHEN_HAS_CURL
std::once_flag g_curl_init_once;
CURLcode g_curl_init_result = CURLE_FAILED_INIT;

void init_curl_global()
{
    g_curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT);
}

struct UploadStatus
{
    std::string payload;
    std::size_t offset;
};

std::size_t read_callback(char *ptr, std::size_t size, std::size_t nmemb, void *userdata)
{
    UploadStatus *upload = static_cast<UploadStatus *>(userdata);
    const std::size_t available = upload->payload.size() - upload->offset;
    const std::size_t capacity = size * nmemb;
    const std::size_t bytes = available < capacity ? available : capacity;
    if (bytes > 0)
    {
        std::memcpy(ptr, upload->payload.data() + upload->offset, bytes);
        upload->offset += bytes;
    }
    return bytes;
}
#endif
}

bool SmtpClient::send_verification_code(const std::string &to,
                                        const std::string &code,
                                        std::string &detail)
{
#if !XIAOCHEN_HAS_CURL
    (void)to;
    (void)code;
    detail = "libcurl headers were not found; install libcurl development package and rebuild";
    return false;
#else
    const std::string url = getenv_string("XIAOCHEN_SMTP_URL", "SMTP_URL");
    const std::string username = getenv_string("XIAOCHEN_SMTP_USER", "SMTP_USER");
    const std::string password = getenv_string("XIAOCHEN_SMTP_PASSWORD", "SMTP_PASSWORD");
    std::string from = getenv_string("XIAOCHEN_SMTP_FROM", "SMTP_FROM");
    const std::string from_name = getenv_string("XIAOCHEN_SMTP_FROM_NAME", "SMTP_FROM_NAME");
    const std::string login_options = getenv_string("XIAOCHEN_SMTP_LOGIN_OPTIONS", "SMTP_LOGIN_OPTIONS");

    if (from.empty())
    {
        from = username;
    }

    if (url.empty() || username.empty() || password.empty() || from.empty())
    {
        detail = "SMTP is not configured; set XIAOCHEN_SMTP_URL, XIAOCHEN_SMTP_USER, XIAOCHEN_SMTP_PASSWORD and XIAOCHEN_SMTP_FROM";
        return false;
    }

    std::call_once(g_curl_init_once, init_curl_global);
    if (g_curl_init_result != CURLE_OK)
    {
        detail = std::string("curl global init failed: ") + curl_easy_strerror(g_curl_init_result);
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        detail = "curl easy init failed";
        return false;
    }

    UploadStatus upload;
    upload.payload = build_message(from, from_name, to, code);
    upload.offset = 0;

    struct curl_slist *recipients = nullptr;
    const std::string envelope_from = "<" + from + ">";
    const std::string envelope_to = "<" + to + ">";
    recipients = curl_slist_append(recipients, envelope_to.c_str());
    if (!recipients)
    {
        curl_easy_cleanup(curl);
        detail = "failed to allocate SMTP recipient list";
        return false;
    }

    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, login_options.empty() ? "AUTH=LOGIN" : login_options.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, envelope_from.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    if (env_truthy("XIAOCHEN_SMTP_INSECURE_SKIP_VERIFY"))
    {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    const CURLcode result = curl_easy_perform(curl);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK)
    {
        detail = error_buffer[0] ? error_buffer : curl_easy_strerror(result);
        return false;
    }

    detail.clear();
    return true;
#endif
}
