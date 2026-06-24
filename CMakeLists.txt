cmake_minimum_required(VERSION 3.15)
project(MyCrackDB CXX)

find_package(RocksDB CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)

add_executable(MyCrackDB main.cpp)

target_link_libraries(MyCrackDB PRIVATE RocksDB::rocksdb OpenSSL::Crypto)
target_compile_features(MyCrackDB PRIVATE cxx_std_17)