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
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <vector>
#include <csignal>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

// Global flag for graceful shutdown
std::atomic<bool> g_shutdown_requested{ false };

// Signal handler function
void signal_handler(int signum) {
    g_shutdown_requested = true;
    std::cout << "\n[!] Interrupt received (Signal " << signum << "). Initiating graceful shutdown...\n";
}

// Convert binary hash to hex string (because Hash algorithms return raw bytes)
std::string to_hex(const std::string& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (char c : data) {
        oss << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(c));
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
// NOTE: Takes a pre-resolved const EVP_MD* instead of an algorithm name string.
// EVP_get_digestbyname() is a lookup into OpenSSL's internal digest table and is
// no longer called here; it's resolved once at startup (see init_algorithm_digests())
// instead of once per hash, per word, for every algorithm.
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

// Cached EVP_MD* pointers, index-aligned with `algorithms`. Populated once by
// init_algorithm_digests() at program startup so hot loops never need to call
// EVP_get_digestbyname() again.
static std::vector<const EVP_MD*> algo_mds;

// Resolve every algorithm name to its EVP_MD* exactly once.
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

void GenerateAndStoreHashes(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles, rocksdb::Status& s, const std::string& plaintext, uint64_t& current_id)
{
    try {
        // Compute BLAKE2b-512 first If it exist in CF1 Skip (also it is faster in generation)
        // MD5 Maybe will produce collision
        // Example:
        // first: TEXTCOLLBYfGiJUETHQ4hAcKSMd5zYpgqf1YRDhkmxHkhPWptrkoyz28wnI9V0aHeAuaKnak
        // second: TEXTCOLLBYfGiJUETHQ4hEcKSMd5zYpgqf1YRDhkmxHkhPWptrkoyz28wnI9V0aHeAuaKnak
        // This avoids generating saving massive space.
        std::string BLAKE2b_512_hash = compute_hash_bin(plaintext, algo_mds[12]);
        std::string existing_id;
        s = db->Get(rocksdb::ReadOptions(), handles[13], BLAKE2b_512_hash, &existing_id);
        if (s.ok())
        {
            std::clog << "Already Generated\n";
            std::string lookup_plain;
            s = db->Get(rocksdb::ReadOptions(), handles[0], existing_id, &lookup_plain);
            if (lookup_plain == plaintext)
            {
                return;
            }
            std::clog << "BLAKE2b_512_hash Collision Detected (very not common)!: { " << plaintext << " } with { " << lookup_plain << " }\n";
            //generate SHA3-512 (less collisions) and check
            std::string SHA3_512_hash = compute_hash_bin(plaintext, algo_mds[9]);
            std::string existing_sha_id;//dummy
            s = db->Get(rocksdb::ReadOptions(), handles[10], SHA3_512_hash, &existing_sha_id);
            if (s.ok())
            {
                std::clog << "Already Generated\n";
            }
        }

        // Increment the global ID counter.
        current_id++;
        std::string id_str(reinterpret_cast<const char*>(&current_id), sizeof(current_id));

        rocksdb::WriteBatch batch;

        // CF0: Store the uint64_t ID -> Plaintext
        batch.Put(handles[0], id_str, plaintext);

        // CF 1-17: Compute and store remaining algorithms
        for (int i = 0; i < algorithms.size(); ++i) {
            batch.Put(handles[i + 1], compute_hash_bin(plaintext, algo_mds[i]), id_str);
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
    // Safe flush even on error exits just in case
    rocksdb::FlushOptions flush_opts;
    flush_opts.wait = true;
    for (auto h : handles) db->Flush(flush_opts, h);
    db->Close();

    for (auto h : handles) db->DestroyColumnFamilyHandle(h);
    delete db;
    return 1;
}

void print_help() {
    std::cout <<
        "Usage:\n"
        " MyCrackDB -h or --help                  : show help\n"
        " MyCrackDB -l or --lookup <hash>         : lookup hash\n"
        " MyCrackDB -ls or --list                 : display available hash algorithms\n"
        " MyCrackDB -c or --count                 : count the total number of words in the database\n"
        " MyCrackDB -g <text>                     : generate and store <text>\n"
        " MyCrackDB -g <text> -d <algo1,algo2,..> : generate, store and display hashes for <text>\n"
        "         (Don't add spaces between algos if no algorithm selected deafult is all)*\n"
        "                    (Example: MyCrackDB -g ahmed -d MD5,SHA1)*\n"
        " MyCrackDB -g -w wordlist.txt            : generate and store each line from file as a value\n"
        "\nExample: MyCrackDB -l 5d41402abc4b2a76b9719d911017c592\n";
}

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
            std::ifstream f(filename, std::ios::binary);
            if (!f) { std::cerr << "Can't open " << filename << "\n"; return close_db_and_exit_with_error(db, handles); }

            unsigned int num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 4;
            std::cout << "Using " << num_threads << " threads\n";

            std::queue<std::string> work_q;
            std::mutex q_mutex;
            std::mutex db_mutex; // Protects only DB point lookups, ID increment, and Writes

            std::condition_variable cv_consumer;
            std::condition_variable cv_producer;

            std::atomic<bool> done_reading{ false };
            std::atomic<size_t> done{ 0 };

            auto worker = [&]() {
                while (true) {
                    std::string line;
                    {
                        std::unique_lock<std::mutex> lk(q_mutex);
                        cv_consumer.wait(lk, [&] { return !work_q.empty() || done_reading.load(); });
                        if (work_q.empty() && done_reading.load()) break;
                        if (work_q.empty()) continue;

                        line = std::move(work_q.front());
                        work_q.pop();
                    }

                    // OPTIMIZATION: Wake up the producer thread because room just freed up in the queue
                    cv_producer.notify_one();

                    try {
                        // 1. CPU WORK (NO LOCKS): Calculate ALL 17 hashes completely in parallel
                        std::vector<std::string> computed_hashes;
                        computed_hashes.reserve(algorithms.size());

                        for (size_t i = 0; i < algorithms.size(); ++i) {
                            computed_hashes.push_back(compute_hash_bin(line, algo_mds[i]));
                        }
                        std::string blake2b_hash = computed_hashes[12];

                        // 2. DB & CRITICAL TRANSACTION WORK (SHORT LOCK): 
                        {
                            std::lock_guard<std::mutex> lk(db_mutex);

                            std::string existing_id;
                            rocksdb::Status local_s = db->Get(rocksdb::ReadOptions(), handles[13], blake2b_hash, &existing_id);

                            if (!local_s.ok()) { // Word is unique, store it
                                current_id_count++;
                                std::string id_str(reinterpret_cast<const char*>(&current_id_count), sizeof(current_id_count));

                                rocksdb::WriteBatch batch;
                                batch.Put(handles[0], id_str, line);

                                for (size_t i = 0; i < algorithms.size(); ++i) {
                                    batch.Put(handles[i + 1], computed_hashes[i], id_str);
                                }
                                if (current_id_count % 50000 == 0) {
                                    batch.Put(handles[18], "ID_COUNT", id_str);
                                }

                                rocksdb::WriteOptions write_options;
                                write_options.disableWAL = true;
                                local_s = db->Write(write_options, &batch);

                                if (!local_s.ok()) {
                                    std::cerr << "DB Write Error: " << local_s.ToString() << '\n';
                                }
                            }
                        }
                    }
                    catch (const std::exception& ex) {
                        std::cerr << "Worker Error: " << ex.what() << "\n";
                    }

                    size_t c = ++done;
                    if (c % 50000 == 0) {
                        std::cout << "\rProcessed " << c << " lines..." << std::flush;
                    }
                }
                };

            // Spin up the worker pool
            std::vector<std::thread> pool;
            for (unsigned int i = 0; i < num_threads; ++i) pool.emplace_back(worker);

            // Producer loop: Read file lines safely
            std::string line;
            while (std::getline(f, line)) {

                // Break out of file reading if interrupt received
                if (g_shutdown_requested) {
                    std::cout << "\nStopping file read early. Waiting for workers to finish current queue...\n";
                    break;
                }

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty()) {
                    std::unique_lock<std::mutex> lk(q_mutex);
                    // OPTIMIZATION: Bounded queue capacity limit (100,000 items) prevents RAM exhaustion
                    cv_producer.wait(lk, [&] { return work_q.size() < 100000; });

                    work_q.push(std::move(line));
                    cv_consumer.notify_one();
                }
            }

            // Signal termination to workers
            {
                std::lock_guard<std::mutex> lk(q_mutex);
                done_reading = true;
            }
            cv_consumer.notify_all();

            for (auto& t : pool) t.join();

            {
                std::string id_str(reinterpret_cast<const char*>(&current_id_count), sizeof(current_id_count));
                db->Put(rocksdb::WriteOptions(), handles[18], "ID_COUNT", id_str);
            }

            std::cout << "\rProcessed " << done.load() << " lines. Done.\n";
            std::cout << "Loaded " << filename << " Successfully\n";
            std::cout << "Total database words is now: " << current_id_count << std::endl;

            // Skip compaction if user requested a shutdown
            if (!g_shutdown_requested) {
                std::cout << "\nTriggering Post-Import DB Compaction (This may take a while)...\n";
                rocksdb::CompactRangeOptions compact_options;
                for (auto h : handles) {
                    db->CompactRange(compact_options, h, nullptr, nullptr);
                }
                std::cout << "Compaction complete!" << std::endl;
            }
            else {
                std::cout << "\nSkipping compaction due to shutdown request.\n";
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