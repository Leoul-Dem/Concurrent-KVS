// KVS Client Library - Public API for accessing shared memory KVS
#pragma once

#include "task_queue.hpp"
#include <memory>
#include <optional>
#include <string>
#include <atomic>

/**
 * @brief Client-side library interface for Concurrent KVS
 * 
 * This class encapsulates shared memory access and provides a clean API
 * for submitting tasks to the key-value store. Thread-safe for multi-threaded
 * client applications.
 * 
 * Usage:
 *   int mem_fd = ...; // Received from server
 *   KVSClient<int, int> client(mem_fd);
 *   auto result = client.get(42);
 *   client.set(42, 100);
 */
template <typename K, typename V>
class KVSClient {
private:
    // Shared memory mapping
    void* shm_ptr;
    size_t shm_size;
    int shm_fd;
    
    // Pointer to the shared TaskQueue
    TaskQueue<K, V>* task_queue;
    
    // Client identification
    int client_pid;
    
    // Task ID generation (thread-safe)
    std::atomic<int> next_task_id;
    
    // Internal helper to submit a task
    bool submit_task(const Task<K, V>& task);
    
public:
    /**
     * @brief Construct a KVSClient by mapping shared memory
     * @param mem_fd File descriptor for shared memory (received from server) - currently unused, will use shm_open with known name
     * @param pid Client process ID (defaults to current PID)
     * @throws std::runtime_error if mapping fails
     */
    explicit KVSClient(int mem_fd, int pid = -1);
    
    /**
     * @brief Destructor - unmaps shared memory
     */
    ~KVSClient();
    
    // Delete copy constructor and assignment (resource management)
    KVSClient(const KVSClient&) = delete;
    KVSClient& operator=(const KVSClient&) = delete;
    
    // Allow move semantics
    KVSClient(KVSClient&& other) noexcept;
    KVSClient& operator=(KVSClient&& other) noexcept;
    
    /**
     * @brief Submit a GET operation
     * @param key The key to retrieve
     * @return Task ID for tracking this operation
     * @note Currently async - response mechanism to be implemented in Phase 4
     */
    int get(const K& key);
    
    /**
     * @brief Submit a SET operation (update or insert)
     * @param key The key to set
     * @param value The value to assign
     * @return Task ID for tracking this operation
     */
    int set(const K& key, const V& value);
    
    /**
     * @brief Submit a POST operation (insert only, fail if exists)
     * @param key The key to insert
     * @param value The value to assign
     * @return Task ID for tracking this operation
     */
    int post(const K& key, const V& value);
    
    /**
     * @brief Submit a DELETE operation
     * @param key The key to delete
     * @return Task ID for tracking this operation
     */
    int del(const K& key);
    
    /**
     * @brief Check if the task queue is accessible
     * @return true if the client is properly connected
     */
    bool is_connected() const;
    
    /**
     * @brief Get the current task queue size (approximate)
     * @return Number of pending tasks in the queue
     */
    size_t queue_size() const;
    
    /**
     * @brief Check if the task queue is full
     * @return true if queue cannot accept more tasks
     */
    bool is_queue_full() const;
};

// Implementation must be in header for templates
#include "kvs_client_impl.hpp"
