cmake_minimum_required(VERSION 3.15)
project(MyCrackDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(RocksDB CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(MyCrackDB
    src/main.cpp
    src/common.cpp
    src/hashing.cpp
    src/db_ops.cpp
    src/wordlist_import.cpp
)

target_include_directories(MyCrackDB PRIVATE include)

target_link_libraries(MyCrackDB PRIVATE
    RocksDB::rocksdb
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_features(MyCrackDB PRIVATE cxx_std_17)