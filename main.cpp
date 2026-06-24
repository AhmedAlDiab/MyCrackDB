#include <rocksdb/db.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>
#include <rocksdb/filter_policy.h>
#include <iostream>
#include <cassert>
#include <string>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

// Convert binary hash to hex string (because Hash algorithms return raw bytes)
std::string to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

// Convert hex input into raw binary for database lookup
std::string from_hex(const std::string& hex) {
    std::string binary;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), nullptr, 16);
        binary.push_back(byte);
    }
    return binary;
}

// Compute hash using OpenSSL EVP API 
std::string compute_hash_bin(const std::string& input, const std::string& algo_name) {
    // Load digest algorithm
    const EVP_MD* md = EVP_get_digestbyname(algo_name.c_str());
    if (!md) {
        throw std::runtime_error("Unknown hash algorithm: " + algo_name);
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

void GenerateAndStoreHashes(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles, rocksdb::Status& s, const std::string& plaintext, uint64_t& current_id)
{
    static const std::vector<std::string> algorithms =
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

    try {
        // Compute MD5 first If i exists in CF1 Skip
        // This avoids generating saving massive space.
        std::string md5_hash = compute_hash_bin(plaintext, algorithms[0]);
        std::string existing_id;
        s = db->Get(rocksdb::ReadOptions(), handles[1], md5_hash, &existing_id);
        if (s.ok()) return;

        // Increment the global ID counter.
        current_id++;
        std::string id_str(reinterpret_cast<const char*>(&current_id), sizeof(current_id));

        rocksdb::WriteBatch batch;

        // CF0: Store the uint64_t ID -> Plaintext
        batch.Put(handles[0], id_str, plaintext);

        // CF 1: Store the MD5 (since we already computed it for the existence check)
        batch.Put(handles[1], md5_hash, id_str);

        // CF 2-17: Compute and store remaining algorithms
        for (int i = 1; i < algorithms.size(); ++i) {
            batch.Put(handles[i + 1], compute_hash_bin(plaintext, algorithms[i]), id_str);
        }

        // CF 18 (Misc): Update the tracker with the newest highest ID
        batch.Put(handles[18], "ID_COUNT", id_str);

        // Disable WAL for massive bulk-loading write speed increase
        rocksdb::WriteOptions write_options;
        write_options.disableWAL = true;
        s = db->Write(write_options, &batch);

        if (!s.ok())
        {
            std::cerr << s.ToString() << '\n';
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
    }
}

int close_db_and_exit_with_error(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles)
{
    for (auto h : handles) db->DestroyColumnFamilyHandle(h);
    delete db;
    return 1;
}

void print_help() {
    std::cout <<
        "Usage:\n"
        " MyCrackDB -h or --help : show help\n"
        " MyCrackDB -l <hash> : lookup hash\n"
        " MyCrackDB -c : count the total number of words in the database\n"
        " MyCrackDB -g <text> : generate and store <text>\n"
        " MyCrackDB -g -w wordlist.txt : generate and store each line from file as a value\n"
        "\nExample: MyCrackDB -l 5d41402abc4b2a76b9719d911017c592\n";
}

int main(int argc, char* argv[]) {
    //TODO: implement UI (idk how but maybe web crow + json + html + js)    
    //TODO: threads for hashing????    
    if (argc < 2) { print_help(); return 1; }
    // UTF-8 support for windows console
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    options.create_missing_column_families = true;

    // Engine Config Tuning "see rocksdb docs"
    options.use_direct_io_for_flush_and_compaction = true;
    options.OptimizeForPointLookup(4096);

    rocksdb::BlockBasedTableOptions table_options;
    table_options.block_size = 16 * 1024;
    table_options.block_cache = rocksdb::NewLRUCache(512 * 1024 * 1024);
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10));

    rocksdb::ColumnFamilyOptions cf_options;
    cf_options.OptimizeForPointLookup(4096);
    cf_options.compression = rocksdb::kZSTD;
    cf_options.bottommost_compression = rocksdb::kZSTD;
    cf_options.compaction_style = rocksdb::kCompactionStyleUniversal;
    cf_options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

    // CF0=ID, CF1-17=Hashes, CF18=Misc
    std::vector<std::string> cf_names = {
        rocksdb::kDefaultColumnFamilyName,
        "MD5","SHA1","SHA2-256","SHA2-512","SHA2-384","SHA2-224","SHA2-512/256","SHA2-512/224","SHA3-256","SHA3-512","SHA3-224","SHA3-384","BLAKE2B-512","BLAKE2S-256","RIPEMD-160","SM3","MD5-SHA1",
        "Misc"
    };

    std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
    for (const auto& name : cf_names) {
        column_families.push_back(rocksdb::ColumnFamilyDescriptor(name, cf_options));
    }

    std::vector<rocksdb::ColumnFamilyHandle*> handles;
    rocksdb::Status s = rocksdb::DB::Open(options, "HashesDB", column_families, &handles, &db);
    if (!s.ok()) {
        std::cerr << "Fatal: Could not open database. " << s.ToString() << "\n";
        return 1;
    }

    // Fetch the current ID count from CF 18 (Misc) on boot
    uint64_t current_id_count = 0;
    std::string count_val;
    rocksdb::Status count_s = db->Get(rocksdb::ReadOptions(), handles[18], "ID_COUNT", &count_val);
    if (count_s.ok() && count_val.size() == sizeof(uint64_t)) {
        std::memcpy(&current_id_count, count_val.data(), sizeof(uint64_t));
    }

    std::string arg = argv[1];

    if (arg == "-h" || arg == "--help") {
        print_help();
    }
    else if (arg == "-c") {
        std::cout << "Total words in database: " << current_id_count << std::endl;
    }
    else if (arg == "-l") {
        if (argc < 3) { std::cerr << "Missing hash\n"; return close_db_and_exit_with_error(db, handles); }
        std::string cmd_hex = argv[2];

        std::string cmd_bin;
        try { cmd_bin = from_hex(cmd_hex); }
        catch (...) {
            std::cerr << "Invalid hex input.\n";
            return close_db_and_exit_with_error(db, handles);
        }

        bool Found = false;
        // Search only hash CFs (1 to 17)
        for (int i = 1; i <= 17; i++)
        {
            std::string value;
            s = db->Get(rocksdb::ReadOptions(), handles[i], cmd_bin, &value);
            if (s.ok()) {
                Found = true;

                // Lookup plaintext from CF 0 using the retrieved ID
                std::string plaintext_result;
                rocksdb::Status s2 = db->Get(rocksdb::ReadOptions(), handles[0], value, &plaintext_result);

                if (s2.ok()) {
                    std::cout << "value: " << plaintext_result << std::endl;
                }
                else {
                    std::cout << "value: [ID Match found, but missing Plaintext in CF0]" << std::endl;
                }

                std::cout << "Algorithm: " << handles[i]->GetName() << std::endl;
            }
        }
        if (!Found) { std::cout << "Hash not found!\n"; }
    }
    else if (arg == "-g") {
        if (argc < 3) { std::cerr << "Missing text or -w file\n"; return close_db_and_exit_with_error(db, handles); }

        std::string input = argv[2];

        if (input == "-w") {
            if (argc < 4) { std::cerr << "Missing filename after -w\n"; return close_db_and_exit_with_error(db, handles); }
            std::string filename = argv[3];
            std::ifstream f(filename, std::ios::binary);
            if (!f) { std::cerr << "Can't open " << filename << "\n"; return close_db_and_exit_with_error(db, handles); }

            std::string line;
            size_t done = 0;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty()) {
                    GenerateAndStoreHashes(db, handles, s, line, current_id_count);
                }
                if (++done % 50000 == 0) {
                    std::cout << "\rProcessed " << done << " lines..." << std::flush;
                }
            }
            std::cout << "\rProcessed " << done << " lines. Done.\n";
            std::cout << "Loaded " << filename << " Successfully\n";
            std::cout << "Total database words is now: " << current_id_count << std::endl;

            std::cout << "\nTriggering Post-Import DB Compaction (This may take a while)...\n";
            rocksdb::CompactRangeOptions compact_options;
            for (auto h : handles) {
                db->CompactRange(compact_options, h, nullptr, nullptr);
            }
            std::cout << "Compaction complete!" << std::endl;
        }
        else {
            GenerateAndStoreHashes(db, handles, s, input, current_id_count);
            std::cout << "Generated hashes for: " << input << std::endl;
            std::cout << "Total database words is now: " << current_id_count << std::endl;
        }
    }
    else {
        print_help();
    }

    for (auto h : handles) db->DestroyColumnFamilyHandle(h);
    delete db;
    return 0;
}