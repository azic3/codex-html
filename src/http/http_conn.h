#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <map>
#include <string>

class HttpConn
{
public:
    struct Request
    {
        std::string method;
        std::string raw_target;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string client_ip;
    };

    struct Response
    {
        int status_code;
        std::string status_text;
        std::string content_type;
        std::string body;
        std::map<std::string, std::string> headers;

        std::string to_http_string() const;
    };

public:
    bool parse_request(const std::string &raw_request, Request &request) const;
    std::map<std::string, std::string> parse_form_urlencoded(const std::string &body) const;
    std::string normalize_path(const std::string &path) const;
    std::string url_decode(const std::string &value) const;
};

#endif
