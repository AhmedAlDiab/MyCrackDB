#include "db_ops.h"
#include "hashing.h"
#include <iostream>

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