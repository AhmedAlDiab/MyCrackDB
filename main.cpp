#include <rocksdb/db.h>
#include <iostream>
#include <cassert>
#include <string>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <fstream>
#include <sstream>
#include <iomanip>

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

// Compute hash using OpenSSL EVP API ... Complex stuff
std::string compute_hash(const std::string& input, const std::string& algo_name) {
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
        return to_hex(hash, hash_len);
    }
    catch (...) {
        EVP_MD_CTX_free(ctx);
        throw; // rethrow exception
    }
}

void GenerateAndStoreHashes(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles , rocksdb::Status& s, const std::string& plaintext)
{
    //save time by skipping the generated ones
    std::string value;
    s = db->Get(rocksdb::ReadOptions(), handles[0], plaintext, &value);
    if (s.ok()) return;

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
        rocksdb::WriteBatch batch;
        for (int i = 0; i < algorithms.size(); ++i) {
            batch.Put(handles[i + 1], compute_hash(plaintext, algorithms[i]), plaintext);
        }
        batch.Put(handles[0], plaintext, "1");
        s = db->Write(rocksdb::WriteOptions(), &batch);
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
    // DB definition with families for hash algorithms
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    options.create_missing_column_families = true;
    std::vector<std::string> cf_names = {
        rocksdb::kDefaultColumnFamilyName,
        "MD5","SHA1","SHA2-256","SHA2-512","SHA2-384","SHA2-224","SHA2-512/256","SHA2-512/224","SHA3-256","SHA3-512","SHA3-224","SHA3-384","BLAKE2B-512","BLAKE2S-256","RIPEMD-160","SM3","MD5-SHA1"
    };
    std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
    for (const auto& name : cf_names) {
        column_families.push_back(rocksdb::ColumnFamilyDescriptor(name, rocksdb::ColumnFamilyOptions()));
    }
    std::vector<rocksdb::ColumnFamilyHandle*> handles;
    rocksdb::Status s = rocksdb::DB::Open(options, "HashesDB", column_families, &handles, &db);
    if (!s.ok()) {
        std::cerr << "Fatal: Could not open database. " << s.ToString() << "\n";
        return 1;
    }    
    std::string arg = argv[1];

    if (arg == "-h" || arg == "--help") {
        print_help();
    }
    else if (arg == "-l") {
        if (argc < 3) { std::cerr << "Missing hash\n"; return close_db_and_exit_with_error(db,handles); }
        std::string cmd = argv[2];
        std::string value;
        bool Found = false;
        for (auto handle : handles)
        {
            if (handle == handles[0]) continue;
            s = db->Get(rocksdb::ReadOptions(), handle, cmd, &value);
            if (s.ok()) {
                Found = 1;
                std::cout << "value: " << value << std::endl;
                std::cout << "Algorithm: " << handle->GetName() << std::endl;
            }            
        }
        if (!Found)
        {
            std::cout << "Hash not found!\n";
        }
    }
    else if (arg == "-g") {
        if (argc < 3) { std::cerr << "Missing text or -w file\n"; return close_db_and_exit_with_error(db, handles); }

        std::string input = argv[2];

        // file mode: -g -w wordlist.txt
        if (input == "-w") {
            if (argc < 4) { std::cerr << "Missing filename after -w\n"; return close_db_and_exit_with_error(db, handles); }
            std::string filename = argv[3];
            std::ifstream f(filename);
            if (!f) { std::cerr << "Can't open " << filename << "\n"; return close_db_and_exit_with_error(db, handles); }

            std::string line;
            size_t done = 0;
            while (std::getline(f, line)) {
                // Windows CR
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty()) {
                    GenerateAndStoreHashes(db, handles, s, line);
                }
                if (++done % 50000 == 0) {
                    std::cout << "\rProcessed " << done << " lines..." << std::flush;
                }
            }
            std::cout << "\rProcessed " << done << " lines. Done.\n";
            std::cout << "Loaded " << filename << " Successfully" << std::endl;
        }
        else {
            // single text mode: -g mytext
            GenerateAndStoreHashes(db, handles, s, input);
            std::cout << "Generated hashes for: " << input << std::endl;
        }
    }
    else {
        print_help();
    }
    
    for (auto h : handles) db->DestroyColumnFamilyHandle(h);
    delete db;
    return 0;
}
