#pragma once
#include <rocksdb/db.h>
#include <cstdint>
#include <string>
#include <vector>

void GenerateAndStoreHashes(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles, rocksdb::Status& s, const std::string& plaintext, uint64_t& current_id);

int close_db_and_exit_with_error(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles);