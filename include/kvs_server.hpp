// KVS Server Library - Public API for server-side processing
#pragma once

#include "task_queue.hpp"
#include "response_table.hpp"
#include "concurrent_hashmap.hpp"
#include <vector>
#include <thread>
#include <atomic>

/**
 * @brief Server-side library interface for Concurrent KVS
 * 
 * This class encapsulates the worker thread pool and ConcurrentHashMap,
 * providing a clean API for processing tasks from the shared memory queue.
 * 
 * Usage:
 *   TaskQueue<int, int>* queue = ...; // From shared memory
 *   KVSServer<int, int> server(queue);
 *   server.start(4);  // Start 4 worker threads
 *   // ... let it run ...
 *   server.stop();    // Graceful shutdown
 */
template <typename K, typename V>
class KVSServer {
private:
    // Storage for key-value pairs
    ConcurrentHashMap<K, V> storage;
    
    // Pointer to the shared TaskQueue (in shared memory)
    TaskQueue<K, V>* task_queue;
    
    // Pointer to the shared ResponseTable (in shared memory)
    ResponseTable<V>* response_table;
    
    // Worker thread pool
    std::vector<std::thread> workers;
    
    // Server state
    std::atomic<bool> running;
    
    // Worker thread function - continuously processes tasks
    void worker_thread();
    
    // Process a single task from the queue
    void process_task(const Task<K, V>& task);
    
public:
    /**
     * @brief Construct a KVSServer with a task queue and response table
     * @param queue Pointer to shared memory TaskQueue
     * @param responses Pointer to shared memory ResponseTable
     * @param stripe_count Number of lock stripes for ConcurrentHashMap (defaults to hardware concurrency)
     */
    explicit KVSServer(TaskQueue<K, V>* queue, ResponseTable<V>* responses, size_t stripe_count = std::thread::hardware_concurrency());
    
    /**
     * @brief Destructor - ensures workers are stopped
     */
    ~KVSServer();
    
    // Delete copy constructor and assignment (resource management)
    KVSServer(const KVSServer&) = delete;
    KVSServer& operator=(const KVSServer&) = delete;
    
    /**
     * @brief Start the worker thread pool
     * @param num_threads Number of worker threads to spawn
     * @return true if started successfully, false if already running
     */
    bool start(size_t num_threads);
    
    /**
     * @brief Stop the worker thread pool gracefully
     * Waits for all workers to finish their current task
     */
    void stop();
    
    /**
     * @brief Check if the server is currently running
     * @return true if worker threads are active
     */
    bool is_running() const;
    
    /**
     * @brief Get the number of active worker threads
     * @return Number of workers
     */
    size_t worker_count() const;
    
    /**
     * @brief Get the current size of the storage
     * @return Number of key-value pairs stored
     */
    size_t storage_size() const;
};

// Implementation must be in header for templates
#include "kvs_server_impl.hpp"
