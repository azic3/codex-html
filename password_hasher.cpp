#include "password_hasher.h"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__has_include)
#if __has_include(<crypt.h>)
#define XIAOCHEN_HAS_CRYPT 1
#include <crypt.h>
#else
#define XIAOCHEN_HAS_CRYPT 0
#endif
#else
#define XIAOCHEN_HAS_CRYPT 0
#endif

namespace
{
const char kSaltAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

std::string random_salt()
{
    unsigned char bytes[16] = {0};
    std::ifstream random("/dev/urandom", std::ios::in | std::ios::binary);
    if (random.good())
    {
        random.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
    }
    else
    {
        unsigned int seed = static_cast<unsigned int>(std::time(nullptr) ^ std::rand());
        for (std::size_t i = 0; i < sizeof(bytes); ++i)
        {
            seed = seed * 1103515245u + 12345u;
            bytes[i] = static_cast<unsigned char>((seed >> 16) & 0xff);
        }
    }

    std::string salt;
    salt.reserve(sizeof(bytes));
    for (std::size_t i = 0; i < sizeof(bytes); ++i)
    {
        salt.push_back(kSaltAlphabet[bytes[i] % (sizeof(kSaltAlphabet) - 1)]);
    }
    return salt;
}

std::string crypt_salt()
{
    return "$6$rounds=100000$" + random_salt() + "$";
}
}

bool PasswordHasher::is_password_hash(const std::string &stored_password)
{
    return stored_password.compare(0, 3, "$6$") == 0;
}

bool PasswordHasher::hash_password(const std::string &password, std::string &hash_out, std::string &detail)
{
#if !XIAOCHEN_HAS_CRYPT
    (void)password;
    hash_out.clear();
    detail = "system crypt password hashing support is unavailable in this build";
    return false;
#else
    const std::string salt = crypt_salt();
    struct crypt_data data;
    data.initialized = 0;

    char *result = crypt_r(password.c_str(), salt.c_str(), &data);
    if (result == nullptr || result[0] == '\0')
    {
        hash_out.clear();
        detail = "password hashing failed";
        return false;
    }

    hash_out = result;
    detail.clear();
    return true;
#endif
}

bool PasswordHasher::verify_password(const std::string &password,
                                     const std::string &stored_password,
                                     bool &matched,
                                     bool &needs_rehash,
                                     std::string &detail)
{
    matched = false;
    needs_rehash = false;

    if (!is_password_hash(stored_password))
    {
        matched = (password == stored_password);
        needs_rehash = matched;
        detail.clear();
        return true;
    }

#if !XIAOCHEN_HAS_CRYPT
    (void)password;
    detail = "system crypt password hashing support is unavailable in this build";
    return false;
#else
    struct crypt_data data;
    data.initialized = 0;

    char *result = crypt_r(password.c_str(), stored_password.c_str(), &data);
    if (result == nullptr || result[0] == '\0')
    {
        detail = "password verification failed";
        return false;
    }

    matched = (stored_password == result);
    detail.clear();
    return true;
#endif
}
