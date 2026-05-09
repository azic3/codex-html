#ifndef SMTP_CLIENT_H
#define SMTP_CLIENT_H

#include <string>

class SmtpClient
{
public:
    static bool send_verification_code(const std::string &to,
                                       const std::string &code,
                                       std::string &detail);
};

#endif
