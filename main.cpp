#include <rocksdb/db.h>
#include <iostream>
#include <cassert>

#ifdef _WIN32
#include <windows.h>
#endif

void GenerateAndStoreHashes(rocksdb::DB* db, const std::vector<rocksdb::ColumnFamilyHandle*>& handles, const std::string& plaintext) 
{
    //Maybe gen structure
}

int main() {
    //TODO: implement arrgs (-h , -l lookup , -g generate "takes file or string")

    // UTF-8 support for windows console
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // DB definition
    rocksdb::DB* db;
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::Status s = rocksdb::DB::Open(options, "HashsDB", &db);
    assert(s.ok());

    //TODO: Core ideas (Generate "Openssl" and Lookup(rocksdb))
    // -- CF for every algorithm
    // -- lookup by hash and seach all CFs (or the ones with same len) and output the type with the value
    //MAYBE: UI (idk how but maybe web crow + json + html + js)

    delete db;
    return 0;
}
