# LibMagelessChain

LibMagelessChain is a C++ library designed for building specialized blockchain databases. Unlike general-purpose public
blockchains, LibMagelessChain is intended for creating databases that may be private, semi-private, or public, with
highly customizable structures and designs.

This library focuses on the on-disk implementation, verification logic, serialization, indexing, block building and
optimization, and basic protocol details, but does not provide a network implementation. Any project that uses this
library must provide the following:
- A network protocol for communication between nodes
- A mechanism for synchronizing the chain with other nodes
- Details about what defines a valid transaction, and how transactions alter the chain state
- Specific details on how to serialize transactions (libmagelesschain handles serialization from there)
- What the MEV optimization goal is (libmagelesschain handles MEV optimization and building from there)

## Try it in 5 minutes

Create a `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyChain)

include(FetchContent)

# Add libmagelesschain to the project
FetchContent_Declare(
    libmagelesschain
    GIT_REPOSITORY https://gitea.com/komozoi/libmagelesschain.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libmagelesschain)

add_executable(mychain main.cpp)
target_link_libraries(mychain PRIVATE LibMagelessChain)
```

And create a file `main.cpp`:

```cpp
#include <blockchain/BlockchainFrontend.h>
#include <blockchain/BlockchainBackend.h>
#include <blockchain/Transaction.h>
#include <blockchain/BlockchainStateSnapshot.h>
#include <Logger.h>
#include <iostream>

// 1. Define your State
class MyState : public BlockchainStateSnapshot {
public:
    int counter = 0;
    MyState(BlockchainBackend& backend, long blockHeight) 
        : BlockchainStateSnapshot(backend, blockHeight) {}
};

// 2. Define your Transaction
class IncrementTx : public Transaction {
public:
    uint8_t getTypeId() const override { return 1; }
    uint64_t getTimestamp() const override { return 123456789; } 
    
    bool verify(BlockchainStateSnapshot& snapshot) const override { return true; }
    
    bool apply(BlockchainStateSnapshot& snapshot) const override {
        static_cast<MyState&>(snapshot).counter++;
        return true;
    }
    
    float computeValue(BlockchainStateSnapshot& snapshot) const override { return 1.0f; }
    
    void write(MmapHandle* dst) const override { 
        dst->write(getTypeId());
    }
    
    size_t size() const override { return sizeof(uint8_t); }
    
    static sp<Transaction> createFromMmap(MmapHandle* src) { 
        return sp<IncrementTx>::create(); 
    }
};

int main() {
    // libexcessive logger (log directory, file level, console level)
    Logger logger("logs", 0, 0);
    
    // Register transaction types
    Transaction::registerType(1, IncrementTx::createFromMmap);

    // Initialize backend and frontend
    BlockchainBackend backend(logger, "data");
    BlockchainFrontend frontend(backend, sp<MyState>::create(backend, 0));

    // Send a transaction to the mempool
    frontend.sendTransaction(sp<IncrementTx>::create());
    
    // The state in the frontend includes the effects of transactions in the mempool
    std::cout << "Current counter: " 
              << static_cast<const MyState&>(*frontend.getState()).counter << std::endl;

    return 0;
}
```

## Key Features

* **Blockchain Storage**: Managed through a decoupled Frontend/Backend architecture.
    * **Backend**: Handles persistent storage on disk, block processing, and state consistency.
    * **Frontend**: Provides a database-like abstraction for low-latency read/write access and transaction management.
* **Document Indexing & Querying**: Built-in support for document-based storage with efficient indexing using B-Trees.
  Supports querying by Document ID, time-ranges, and allows indexing other fields.
* **MEV Builder**: A block builder inspired by MEV, which optimizes blocks for the highest "value". Value does not
  necessarily mean profit though, it could be maximizing transaction success, data completeness, conciseness, or other metrics.
* **Customizable Design**: Flexible framework allowing developers to define their own transaction types, verification
  logic, and state transitions for non-traditional blockchain use cases.

## Architecture

LibMagelessChain separates the **Frontend** and **Backend** to balance performance and reliability:

- **Frontend**: The primary database-like interface for applications. It provides low-latency access to the mempool and
  current state, abstracting the underlying block structure for real-time data use.
- **Backend**: The persistence layer. It manages on-disk storage using "Epoch" files, handles block processing, and
  maintains the long-term immutable record.

## Dependencies

The project relies on the following libraries:

- [**libexcessive**](https://github.com/komozoi/libexcessive): Core utility and data structure library providing 
  high-performance I/O and smart pointers.
- **GoogleTest**: For the test suite (optional).

## Building

LibMagelessChain uses CMake:

```bash
mkdir build && cd build
cmake ..
make
```

## License

This project is licensed under the Apache License, Version 2.0. See the source file headers for details.
