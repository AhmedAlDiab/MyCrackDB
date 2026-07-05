#include "hashing.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

std::string to_hex(const std::string& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (char c : data) {
        oss << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(c));
    }

    return oss.str();
}

std::string from_hex(const std::string& hex) {
    std::string binary;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), nullptr, 16);
        binary.push_back(byte);
    }
    return binary;
}

std::string compute_hash_bin(const std::string& input, const EVP_MD* md) {
    if (!md) {
        throw std::runtime_error("Null digest algorithm");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    try {
        if (EVP_DigestInit_ex(ctx, md, nullptr) != 1)
            throw std::runtime_error("DigestInit failed");

        if (EVP_DigestUpdate(ctx, input.data(), input.size()) != 1)
            throw std::runtime_error("DigestUpdate failed");

        if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1)
            throw std::runtime_error("DigestFinal failed");

        EVP_MD_CTX_free(ctx);
        // RAW BINARY
        return std::string(reinterpret_cast<char*>(hash), hash_len);
    }
    catch (...) {
        EVP_MD_CTX_free(ctx);
        throw; // rethrow exception
    }
}

const std::vector<std::string> algorithms =
{
    "MD5",
    "SHA1",
    "SHA2-256",
    "SHA2-512",
    "SHA2-384",
    "SHA2-224",
    "SHA2-512/256",
    "SHA2-512/224",
    "SHA3-256",
    "SHA3-512",
    "SHA3-224",
    "SHA3-384",
    "BLAKE2B-512",
    "BLAKE2S-256",
    "RIPEMD-160",
    "SM3",
    "MD5-SHA1"
};

std::vector<const EVP_MD*> algo_mds;

void init_algorithm_digests() {
    algo_mds.clear();
    algo_mds.reserve(algorithms.size());
    for (const auto& name : algorithms) {
        const EVP_MD* md = EVP_get_digestbyname(name.c_str());
        if (!md) {
            throw std::runtime_error("Unknown hash algorithm: " + name);
        }
        algo_mds.push_back(md);
    }
}