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

## Key Features

* **Blockchain Storage**: Managed through a decoupled Frontend/Backend architecture.
    * **Backend**: Handles persistent storage on disk, block processing, and state consistency.
    * **Frontend**: Provides a database-like abstraction for low-latency read/write access and transaction management.
* **Document Indexing & Querying**: Built-in support for document-based storage with efficient indexing using B-Trees.
  Supports querying by Document ID, time-ranges, and allows indexing other fields.
* **MEV Builder**: A block builder inspired by MEV, which optimizes blocks for the highest "value".  Value does not
  necessarily mean profit though, it could be maximizing transaction success, data completeness, conciseness, or other metrics.
* **Customizable Design**: Flexible framework allowing developers to define their own transaction types, verification
  logic, and state transitions for non-traditional blockchain use cases.

## Architecture

LibMagelessChain separates the **Frontend** and **Backend** to balance performance and reliability:

- **Frontend**: The primary database-like interface for applications. It provides low-latency access to the mempool and
  current state, abstracting the underlying block structure for real-time data use.
- **Backend**: The persistence layer. It manages on-disk storage using "Epoch" files, handles block processing, and
  maintains the long-term immutable record.

## Getting Started

This section needs to be written when the API is stable and mature.

### Dependencies

The project relies on the following libraries:

- **excessive**: Core utility and data structure library
- **GoogleTest**: For the test suite

### Building

LibMagelessChain uses CMake:

```bash
mkdir build && cd build
cmake ..
make
```

## License

This project is licensed under the Apache License, Version 2.0. See the source file headers for details.
