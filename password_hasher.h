#ifndef PASSWORD_HASHER_H
#define PASSWORD_HASHER_H

#include <string>

class PasswordHasher
{
public:
    static bool hash_password(const std::string &password, std::string &hash_out, std::string &detail);
    static bool verify_password(const std::string &password,
                                const std::string &stored_password,
                                bool &matched,
                                bool &needs_rehash,
                                std::string &detail);
    static bool is_password_hash(const std::string &stored_password);
};

#endif
