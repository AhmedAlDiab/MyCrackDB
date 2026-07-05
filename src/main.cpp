#include <rocksdb/db.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>
#include <rocksdb/filter_policy.h>
#include <iostream>
#include <cstring>
#include <csignal>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common.h"
#include "hashing.h"
#include "db_ops.h"
#include "wordlist_import.h"

int main(int argc, char* argv[]) {
    // Register signal handlers for Ctrl+C and termination requests
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    //TODO: implement UI (idk how but maybe web crow + json + html + js)  
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

    // Resolve all EVP_MD* digest pointers once, up front, instead of doing an
    // EVP_get_digestbyname() string lookup on every single hash computation.
    try {
        init_algorithm_digests();
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return close_db_and_exit_with_error(db, handles);
    }

    if (arg == "-h" || arg == "--help") {
        print_help();
    }
    else if (arg == "-ls" || arg == "--list")
    {
        for (int i = 0; i < algorithms.size() - 1; i++)
        {
            std::cout << algorithms[i] << "\n";
        }
        std::cout << algorithms[algorithms.size() - 1] << std::endl;
    }
    else if (arg == "-c" || arg == "--count") {
        std::cout << "Total words in database: " << current_id_count << std::endl;
    }
    else if (arg == "-l" || arg == "--lookup") {
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

            if (!ImportWordlist(db, handles, filename, current_id_count)) {
                return close_db_and_exit_with_error(db, handles);
            }
        }
        else {
            GenerateAndStoreHashes(db, handles, s, input, current_id_count);
            std::cout << "Generated hashes for: " << input << std::endl;
            std::cout << "Total database words is now: " << current_id_count << std::endl;
            if (argc >= 4) {
                std::string display = argv[3];
                if (display == "-d")
                {
                    std::cout << "Hashes for " << input << ":\n";
                    if (argc > 4)
                    {
                        std::string algs = argv[4];
                        std::string tmp;
                        std::map<std::string, int>Freq;
                        for (char& c : algs)c = toupper(c);
                        for (int i = 0; i < algs.length(); i++)
                        {
                            if (algs[i] != ',')
                                tmp += algs[i];
                            else
                            {
                                Freq[tmp]++;
                                tmp.clear();
                            }
                        }
                        if (!tmp.empty()) Freq[tmp]++;
                        for (size_t i = 0; i < algorithms.size(); ++i) {
                            const std::string& alg = algorithms[i];
                            if (Freq.count(alg)) {
                                try {
                                    std::cout << alg << ": " << to_hex(compute_hash_bin(input, algo_mds[i])) << "\n";
                                }
                                catch (const std::exception& e) {
                                    std::cerr << alg << ": [Error] " << e.what() << "\n";
                                }
                            }
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < algorithms.size(); ++i)
                        {
                            try {
                                std::cout << algorithms[i] << ": " << to_hex(compute_hash_bin(input, algo_mds[i])) << "\n";
                            }
                            catch (const std::exception& e) {
                                std::cerr << algorithms[i] << ": [Error] " << e.what() << "\n";
                            }
                        }
                    }
                }
            }
        }
    }
    else {
        print_help();
    }
    rocksdb::FlushOptions flush_opts;
    flush_opts.wait = true;
    for (auto h : handles) {
        db->Flush(flush_opts, h);
    }

    // Destroy handles FIRST while the DB is still open
    for (auto h : handles) {
        db->DestroyColumnFamilyHandle(h);
    }

    // Close the DB SECOND
    rocksdb::Status close_status = db->Close();
    if (!close_status.ok()) {
        std::cerr << "Warning: DB Close failed - " << close_status.ToString() << "\n";
    }

    // Finally, delete the object
    delete db;
    return 0;
}