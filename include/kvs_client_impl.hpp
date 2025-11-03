// KVS Client Library - Implementation (header-only for templates)
#pragma once

#include "kvs_client.hpp"
#include "shared_context.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>
#include <thread>

// Shared memory name - must match server
inline constexpr const char* KVS_SHM_NAME = "/task_queue_shm";

template <typename K, typename V>
KVSClient<K, V>::KVSClient(int mem_fd, int pid)
    : shm_ptr(nullptr)
    , shm_size(sizeof(SharedMemoryContext<K, V>))
    , shm_fd(-1)
    , task_queue(nullptr)
    , response_table(nullptr)
    , client_pid(pid == -1 ? getpid() : pid)
    , next_task_id(1)
    , default_timeout_ms(5000)
{
    // Open the shared memory object by name (not using the passed fd for now)
    // In a future version, we could pass SCM_RIGHTS through UNIX socket
    (void)mem_fd; // Suppress unused parameter warning
    
    shm_fd = shm_open(KVS_SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        throw std::runtime_error(std::string("Failed to open shared memory: ") + strerror(errno));
    }
    
    // Map the shared memory into our address space
    shm_ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (shm_ptr == MAP_FAILED) {
        close(shm_fd);
        throw std::runtime_error(std::string("Failed to map shared memory: ") + strerror(errno));
    }
    
    // Cast the shared memory to SharedMemoryContext pointer
    SharedMemoryContext<K, V>* context = static_cast<SharedMemoryContext<K, V>*>(shm_ptr);
    task_queue = &context->task_queue;
    response_table = &context->response_table;
}

template <typename K, typename V>
KVSClient<K, V>::~KVSClient() {
    if (shm_ptr != nullptr && shm_ptr != MAP_FAILED) {
        munmap(shm_ptr, shm_size);
        shm_ptr = nullptr;
    }
    
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}

template <typename K, typename V>
KVSClient<K, V>::KVSClient(KVSClient&& other) noexcept
    : shm_ptr(other.shm_ptr)
    , shm_size(other.shm_size)
    , shm_fd(other.shm_fd)
    , task_queue(other.task_queue)
    , response_table(other.response_table)
    , client_pid(other.client_pid)
    , next_task_id(other.next_task_id.load())
    , default_timeout_ms(other.default_timeout_ms)
{
    // Invalidate the moved-from object
    other.shm_ptr = nullptr;
    other.task_queue = nullptr;
    other.response_table = nullptr;
    other.shm_fd = -1;
}

template <typename K, typename V>
KVSClient<K, V>& KVSClient<K, V>::operator=(KVSClient&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        if (shm_ptr != nullptr && shm_ptr != MAP_FAILED) {
            munmap(shm_ptr, shm_size);
        }
        
        // Transfer ownership
        shm_ptr = other.shm_ptr;
        shm_size = other.shm_size;
        shm_fd = other.shm_fd;
        task_queue = other.task_queue;
        response_table = other.response_table;
        client_pid = other.client_pid;
        next_task_id.store(other.next_task_id.load());
        default_timeout_ms = other.default_timeout_ms;
        
        // Invalidate the moved-from object
        other.shm_ptr = nullptr;
        other.task_queue = nullptr;
        other.response_table = nullptr;
        other.shm_fd = -1;
    }
    return *this;
}

template <typename K, typename V>
bool KVSClient<K, V>::submit_task(const Task<K, V>& task) {
    if (!is_connected()) {
        return false;
    }
    
    // Clear the response slot before submitting
    response_table->clear_slot(task.task_id);
    
    // Use try_push for non-blocking submission
    // Returns false if queue is full
    return task_queue->try_push(task);
}

template <typename K, typename V>
bool KVSClient<K, V>::wait_for_response(int task_id, Response<V>*& response, int timeout_ms) {
    response = response_table->get_slot(task_id);
    
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);
    
    while (!response->is_completed()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            return false; // Timeout
        }
        
        // Brief sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    return true;
}

// Async methods (return task_id)
template <typename K, typename V>
int KVSClient<K, V>::get_async(const K& key) {
    Task<K, V> task;
    task.cmd = CMD_GET;
    task.key = key;
    task.has_value = false;
    task.client_pid = client_pid;
    task.task_id = next_task_id.fetch_add(1, std::memory_order_relaxed);
    
    if (!submit_task(task)) {
        return -1; // Failed to submit (queue full or disconnected)
    }
    
    return task.task_id;
}

// Synchronous GET (returns value)
template <typename K, typename V>
std::optional<V> KVSClient<K, V>::get(const K& key, int timeout_ms) {
    int task_id = get_async(key);
    if (task_id == -1) {
        return std::nullopt; // Failed to submit
    }
    
    Response<V>* response;
    if (!wait_for_response(task_id, response, timeout_ms)) {
        return std::nullopt; // Timeout
    }
    
    if (response->status.load() == RESPONSE_SUCCESS) {
        return response->value;
    }
    
    return std::nullopt; // Not found
}

template <typename K, typename V>
int KVSClient<K, V>::set_async(const K& key, const V& value) {
    Task<K, V> task;
    task.cmd = CMD_SET;
    task.key = key;
    task.value = value;
    task.has_value = true;
    task.client_pid = client_pid;
    task.task_id = next_task_id.fetch_add(1, std::memory_order_relaxed);
    
    if (!submit_task(task)) {
        return -1;
    }
    
    return task.task_id;
}

// Synchronous SET
template <typename K, typename V>
bool KVSClient<K, V>::set(const K& key, const V& value, int timeout_ms) {
    int task_id = set_async(key, value);
    if (task_id == -1) {
        return false; // Failed to submit
    }
    
    Response<V>* response;
    if (!wait_for_response(task_id, response, timeout_ms)) {
        return false; // Timeout
    }
    
    return response->status.load() == RESPONSE_SUCCESS;
}

template <typename K, typename V>
int KVSClient<K, V>::post_async(const K& key, const V& value) {
    Task<K, V> task;
    task.cmd = CMD_POST;
    task.key = key;
    task.value = value;
    task.has_value = true;
    task.client_pid = client_pid;
    task.task_id = next_task_id.fetch_add(1, std::memory_order_relaxed);
    
    if (!submit_task(task)) {
        return -1;
    }
    
    return task.task_id;
}

// Synchronous POST
template <typename K, typename V>
bool KVSClient<K, V>::post(const K& key, const V& value, int timeout_ms) {
    int task_id = post_async(key, value);
    if (task_id == -1) {
        return false; // Failed to submit
    }
    
    Response<V>* response;
    if (!wait_for_response(task_id, response, timeout_ms)) {
        return false; // Timeout
    }
    
    return response->status.load() == RESPONSE_SUCCESS;
}

template <typename K, typename V>
int KVSClient<K, V>::del_async(const K& key) {
    Task<K, V> task;
    task.cmd = CMD_DELETE;
    task.key = key;
    task.has_value = false;
    task.client_pid = client_pid;
    task.task_id = next_task_id.fetch_add(1, std::memory_order_relaxed);
    
    if (!submit_task(task)) {
        return -1;
    }
    
    return task.task_id;
}

// Synchronous DELETE
template <typename K, typename V>
bool KVSClient<K, V>::del(const K& key, int timeout_ms) {
    int task_id = del_async(key);
    if (task_id == -1) {
        return false; // Failed to submit
    }
    
    Response<V>* response;
    if (!wait_for_response(task_id, response, timeout_ms)) {
        return false; // Timeout
    }
    
    return response->status.load() == RESPONSE_SUCCESS;
}

template <typename K, typename V>
bool KVSClient<K, V>::is_connected() const {
    return task_queue != nullptr && shm_ptr != nullptr && shm_ptr != MAP_FAILED;
}

template <typename K, typename V>
size_t KVSClient<K, V>::queue_size() const {
    if (!is_connected()) {
        return 0;
    }
    return task_queue->size();
}

template <typename K, typename V>
bool KVSClient<K, V>::is_queue_full() const {
    if (!is_connected()) {
        return true; // Can't submit if disconnected
    }
    return task_queue->full();
}
