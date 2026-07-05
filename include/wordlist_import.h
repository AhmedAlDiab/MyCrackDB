#pragma once
#include <rocksdb/db.h>
#include <cstdint>
#include <string>
#include <vector>

// Imports a wordlist file, hashing and storing each unique line across all
// algorithm column families. Returns false if the file could not be opened
// (caller should treat this as a fatal startup error); true otherwise, whether
// the import ran to completion or was cut short by a graceful shutdown request.
bool ImportWordlist(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles, const std::string& filename, uint64_t& current_id_count);