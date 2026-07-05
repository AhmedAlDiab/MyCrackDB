#pragma once
#include <openssl/evp.h>
#include <string>
#include <vector>

// Convert binary hash to hex string (because Hash algorithms return raw bytes)
std::string to_hex(const std::string& data);

// Convert hex input into raw binary for database lookup
std::string from_hex(const std::string& hex);

// Compute hash using OpenSSL EVP API. Takes a pre-resolved const EVP_MD* instead
// of an algorithm name string; resolve names once via init_algorithm_digests().
std::string compute_hash_bin(const std::string& input, const EVP_MD* md);

extern const std::vector<std::string> algorithms;

// Cached EVP_MD* pointers, index-aligned with `algorithms`. Populated once by
// init_algorithm_digests() at program startup so hot loops never need to call
// EVP_get_digestbyname() again.
extern std::vector<const EVP_MD*> algo_mds;

// Resolve every algorithm name to its EVP_MD* exactly once.
void init_algorithm_digests();