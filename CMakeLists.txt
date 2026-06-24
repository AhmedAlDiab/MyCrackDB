cmake_minimum_required(VERSION 3.15)
project(MyCrackDB CXX)

# Find the dependencies Conan just downloaded
find_package(RocksDB REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(MyCrackDB main.cpp)

# Link them to your executable
target_link_libraries(MyCrackDB PRIVATE RocksDB::rocksdb OpenSSL::Crypto)
target_compile_features(MyCrackDB PRIVATE cxx_std_17)