#include "http_conn.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace
{
std::string trim(const std::string &value)
{
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n'))
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
    {
        --end;
    }

    return value.substr(start, end - start);
}
}

std::string HttpConn::Response::to_http_string() const
{
    std::ostringstream stream;
    stream << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    stream << "Content-Type: " << content_type << "\r\n";
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "Connection: close\r\n";
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
    {
        stream << it->first << ": " << it->second << "\r\n";
    }
    stream << "\r\n";
    stream << body;
    return stream.str();
}

bool HttpConn::parse_request(const std::string &raw_request, Request &request) const
{
    const std::size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        return false;
    }

    const std::string header_block = raw_request.substr(0, header_end);
    request.body = raw_request.substr(header_end + 4);

    std::istringstream stream(header_block);
    std::string request_line;
    if (!std::getline(stream, request_line))
    {
        return false;
    }

    if (!request_line.empty() && request_line[request_line.size() - 1] == '\r')
    {
        request_line.erase(request_line.size() - 1);
    }

    std::istringstream first_line(request_line);
    first_line >> request.method >> request.raw_target >> request.version;
    if (request.method.empty() || request.raw_target.empty() || request.version.empty())
    {
        return false;
    }

    request.path = normalize_path(request.raw_target);

    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }

        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        request.headers[key] = value;
    }

    return true;
}

std::map<std::string, std::string> HttpConn::parse_form_urlencoded(const std::string &body) const
{
    std::map<std::string, std::string> fields;
    std::size_t start = 0;

    while (start <= body.size())
    {
        const std::size_t amp = body.find('&', start);
        const std::string pair = body.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        const std::size_t eq = pair.find('=');
        const std::string key = url_decode(eq == std::string::npos ? pair : pair.substr(0, eq));
        const std::string value = url_decode(eq == std::string::npos ? "" : pair.substr(eq + 1));

        if (!key.empty())
        {
            fields[key] = value;
        }

        if (amp == std::string::npos)
        {
            break;
        }
        start = amp + 1;
    }

    return fields;
}

std::string HttpConn::normalize_path(const std::string &path) const
{
    std::string decoded = url_decode(path);
    std::string normalized;

    if (decoded.empty() || decoded[0] != '/')
    {
        decoded = "/" + decoded;
    }

    for (std::size_t i = 0; i < decoded.size(); ++i)
    {
        char ch = decoded[i];
        if (ch == '\\')
        {
            ch = '/';
        }
        if (ch == '?' || ch == '#')
        {
            break;
        }
        normalized.push_back(ch);
    }

    while (normalized.find("//") != std::string::npos)
    {
        normalized.replace(normalized.find("//"), 2, "/");
    }

    if (normalized.empty())
    {
        normalized = "/";
    }

    return normalized;
}

std::string HttpConn::url_decode(const std::string &value) const
{
    std::string result;
    result.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%' && i + 2 < value.size() &&
            std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2])))
        {
            const std::string hex = value.substr(i + 1, 2);
            const char ch = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            result.push_back(ch);
            i += 2;
        }
        else if (value[i] == '+')
        {
            result.push_back(' ');
        }
        else
        {
            result.push_back(value[i]);
        }
    }

    return result;
}
