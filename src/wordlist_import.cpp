#include "wordlist_import.h"
#include "common.h"
#include "hashing.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

bool ImportWordlist(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles, const std::string& filename, uint64_t& current_id_count)
{
    std::ifstream f(filename, std::ios::binary);
    if (!f) { std::cerr << "Can't open " << filename << "\n"; return false; }

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

    return true;
}