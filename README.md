# MyCrackDB
A high-performance C++ tool for generating, storing, and looking up cryptographic hashes. Built with **RocksDB** for ultra-fast, persistent key-value storage and **OpenSSL** for robust hashing, MyCrackDB is optimized to process and ingest massive wordlists efficiently.

## Features

* **17 Supported Algorithms:** Automatically generates hashes for MD5, SHA1, SHA2 (224/256/384/512), SHA3, BLAKE2, RIPEMD160, and SM3 simultaneously.
* **High-Performance Ingestion:** Utilizes `rocksdb::WriteBatch` and bitwise hex conversions to process millions of lines from wordlists with minimal overhead.
* **Smart Deduplication:** Checks for existing plaintexts in the default column family before hashing to save CPU cycles and disk space.
* **Column Family Architecture:** Cleanly separates different hash algorithms into their own RocksDB column families for organized storage and lightning-fast lookups.

## Prerequisites

To build this project, you will need:
* A C++17 compatible compiler (MSVC, GCC, or Clang)
* [Conan](https://conan.io/) (C/C++ Package Manager)

### Required Libraries (Handled by Conan)
* `rocksdb/8.10.0` (or your preferred version)
* `openssl/3.x.x` (Requires OpenSSL 3.0+ for native alias support)

## Building the Project

This project uses **Conan** to manage dependencies. 

1. **Clone the repository:**
```bash
   git clone https://github.com/AhmedAlDiab/MyCrackDB.git
   cd MyCrackDB
```

2. **Install dependencies using Conan:**
Run the following command to download and build the required libraries for C++17:
```bash
conan profile detect --force
conan install . --build=rocksdb --build=zstd -o rocksdb/*:with_zstd=True -s compiler.cppstd=17 --output-folder=conan
```


- finally you can build it.



## Usage
Run the compiled executable from your terminal.
### Basic Commands
```text
Usage:
 MyCrackDB -h or --help                  : show help
 MyCrackDB -l or --lookup <hash>         : lookup hash
 MyCrackDB -ls or --list                 : display available hash algorithms
 MyCrackDB -c or --count                 : count the total number of words in the database
 MyCrackDB -g <text>                     : generate and store <text>
 MyCrackDB -g <text> -d <algo1,algo2,..> : generate, store and display hashes for <text>
         (Don't add spaces between algos if no algorithm selected deafult is all)*
                    (Example: MyCrackDB -g ahmed -d MD5,SHA1)*
 MyCrackDB -g -w wordlist.txt            : generate and store each line from file as a value
```
### Examples
**1. Generate hashes for a single word:**
```bash
./MyCrackDB -g "password123"
```
**2. Generate a word and display it in specific algorithms:**
```bash
./MyCrackDB -g ahmed -d MD5,SHA1
```
**3. Ingest a massive dictionary/wordlist:**
```bash
./MyCrackDB -g -w rockyou.txt
```
**4. Lookup a hash to find its plaintext:**
```bash
./MyCrackDB -l 5d41402abc4b2a76b9719d911017c592
```
*Output:*
```text
value: hello
Algorithm: MD5
```
**5. List all available hash algorithms:**
```bash
./MyCrackDB -ls
```
**6. Check how many words are currently stored:**
```bash
./MyCrackDB -c
```
## Database Storage
Upon first run, the tool will create a folder named `HashesDB` in the directory where the executable is launched. This folder contains the RocksDB database files.
*Note: Ensure you have read/write permissions in the execution directory. Do not run multiple instances of MyCrackDB simultaneously, as RocksDB locks the database directory to a single process.*
