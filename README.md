# Concurrent Key-Value Store Library

A high-performance, thread-safe key-value store library implemented in C++20. Features a lock-free task queue, concurrent hash map with striped locking, and shared memory communication between clients and server processes.

## Description

Concurrent-KVS is a library for building concurrent key-value storage systems with multi-process architecture. It provides:

- **Lock-free task queue** for high-throughput request handling
- **Concurrent hash map** with striped locking for scalable parallel access
- **Shared memory IPC** for efficient inter-process communication
- **Worker thread pool** for parallel request processing
- **Synchronous and asynchronous APIs** for flexible integration
- **Template-based design** supporting arbitrary key-value types

The library uses shared memory to enable multiple client processes to submit operations to a server process, which processes requests using a configurable number of worker threads. Results are returned via a shared response table with timeout support.

### Key Features

- Thread-safe operations (GET, SET, POST, DELETE)
- Configurable worker thread pool
- Response mechanism with timeout support
- Zero-copy shared memory communication
- Header-only template library for easy integration
- Clean separation between library and test harnesses

### Architecture

```
Client Process                    Server Process
    |                                  |
    | KVSClient                        | KVSServer
    |    |                             |    |
    |    v                             |    v
    | TaskQueue  <--- Shared Memory -> | TaskQueue
    |    |                             |    |
    |    v                             |    v
    | ResponseTable <---------------- | Worker Threads
                                       |    |
                                       |    v
                                       | ConcurrentHashMap
```

## Quick Start

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 11+)
- CMake 3.20 or higher
- POSIX-compliant system (Linux, macOS)

### Building

```bash
# Clone the repository
git clone https://github.com/Leoul-Dem/Concurrent-KVS.git
cd Concurrent-KVS

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Running the Demo

Terminal 1 - Start the server:
```bash
./build/kvs_server
```

Terminal 2 - Run the client:
```bash
./build/kvs_client
```

### Running Tests

```bash
./build/test_operations
```

## Usage

### Basic Library Integration

Include the library headers in your project:

```cpp
#include "kvs_client.hpp"
#include "kvs_server.hpp"
```

### Server-Side Usage

```cpp
#include "kvs_server.hpp"
#include "shared_context.hpp"
#include <sys/mman.h>

// Create shared memory
int shm_fd = shm_open("/my_kvs", O_CREAT | O_RDWR, 0666);
size_t shm_size = sizeof(SharedMemoryContext<int, int>);
ftruncate(shm_fd, shm_size);

void* shm_ptr = mmap(0, shm_size, PROT_READ | PROT_WRITE, 
                     MAP_SHARED, shm_fd, 0);

// Initialize shared memory context
auto* context = new (shm_ptr) SharedMemoryContext<int, int>();

// Create and start server
KVSServer<int, int> server(&context->task_queue, &context->response_table);
server.start(4); // Start with 4 worker threads

// Server runs until stopped
// ...

server.stop();
munmap(shm_ptr, shm_size);
shm_unlink("/my_kvs");
```

### Client-Side Usage (Synchronous API)

```cpp
#include "kvs_client.hpp"
#include <iostream>

int main() {
    // Connect to shared memory (fd typically received from server)
    KVSClient<int, int> client(mem_fd, getpid());
    
    // SET operation
    if (client.set(42, 100)) {
        std::cout << "SET successful\n";
    }
    
    // GET operation
    auto value = client.get(42);
    if (value.has_value()) {
        std::cout << "GET returned: " << value.value() << "\n";
    }
    
    // POST operation (insert only)
    if (client.post(43, 200)) {
        std::cout << "POST successful\n";
    } else {
        std::cout << "POST failed - key exists\n";
    }
    
    // DELETE operation
    if (client.del(42)) {
        std::cout << "DELETE successful\n";
    }
    
    return 0;
}
```

### Client-Side Usage (Asynchronous API)

```cpp
#include "kvs_client.hpp"

int main() {
    KVSClient<int, int> client(mem_fd, getpid());
    
    // Submit operations without waiting for results
    int task_id1 = client.set_async(1, 100);
    int task_id2 = client.set_async(2, 200);
    int task_id3 = client.get_async(1);
    
    // Continue with other work...
    
    return 0;
}
```

### Custom Types

The library supports any types that satisfy basic requirements:

```cpp
// String key-value store
KVSClient<std::string, std::string> string_client(mem_fd, getpid());

// Custom struct (must be copyable and hashable)
struct Data {
    int id;
    double value;
};

KVSServer<int, Data> custom_server(&queue, &responses);
```

### Configuration Options

```cpp
// Configure timeout (default: 5000ms)
auto value = client.get(42, 10000); // 10 second timeout

// Configure number of worker threads
server.start(8); // Start with 8 workers

// Configure hash map stripe count for better concurrency
KVSServer<int, int> server(&queue, &responses, 16); // 16 lock stripes
```

### API Reference

#### KVSClient Methods

- `std::optional<V> get(const K& key, int timeout_ms = 5000)`
  - Retrieve value for key, returns empty optional if not found
  
- `bool set(const K& key, const V& value, int timeout_ms = 5000)`
  - Insert or update key-value pair, returns true on success
  
- `bool post(const K& key, const V& value, int timeout_ms = 5000)`
  - Insert only if key doesn't exist, returns false if key exists
  
- `bool del(const K& key, int timeout_ms = 5000)`
  - Delete key, returns true if deleted, false if not found

- Async variants: `get_async()`, `set_async()`, `post_async()`, `del_async()`
  - Return task_id for manual tracking

#### KVSServer Methods

- `bool start(size_t num_threads)`
  - Start worker thread pool, returns true on success
  
- `void stop()`
  - Gracefully stop worker threads
  
- `size_t storage_size() const`
  - Get current number of stored key-value pairs
  
- `bool is_running() const`
  - Check if server is currently running

### Thread Safety

- All client methods are thread-safe and can be called concurrently
- Multiple clients can connect to the same server simultaneously
- Server worker threads safely access shared data structures
- No locks required in client code

### Performance Considerations

- Lock-free queue minimizes contention for task submission
- Striped locking in hash map allows parallel access
- Shared memory eliminates serialization overhead
- Response table size (1024) limits concurrent in-flight requests
- Worker thread count should match available CPU cores

### Error Handling

Operations return false or empty optional on failure. Common failure modes:

- Task queue full (client retries automatically)
- Timeout waiting for response
- Shared memory mapping failure (throws exception in constructor)
- Key not found (GET returns empty optional)
- Key already exists (POST returns false)

### Limitations

- Maximum 1024 concurrent in-flight requests per client
- Keys and values must be copyable types
- Single server process per shared memory region
- POSIX systems only (Linux, macOS)

## License

This project is available for educational and research purposes.

## Contributing

Contributions are welcome. Please ensure code follows existing style and includes tests.
