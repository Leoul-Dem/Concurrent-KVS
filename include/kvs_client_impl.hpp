// KVS Client Library - Implementation (header-only for templates)
#pragma once

#include "kvs_client.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>

// Shared memory name - must match server
inline constexpr const char* KVS_SHM_NAME = "/task_queue_shm";

template <typename K, typename V>
KVSClient<K, V>::KVSClient(int mem_fd, int pid)
    : shm_ptr(nullptr)
    , shm_size(sizeof(TaskQueue<K, V>))
    , shm_fd(-1)
    , task_queue(nullptr)
    , client_pid(pid == -1 ? getpid() : pid)
    , next_task_id(1)
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
    
    // Cast the shared memory to TaskQueue pointer
    task_queue = static_cast<TaskQueue<K, V>*>(shm_ptr);
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
    , client_pid(other.client_pid)
    , next_task_id(other.next_task_id.load())
{
    // Invalidate the moved-from object
    other.shm_ptr = nullptr;
    other.task_queue = nullptr;
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
        client_pid = other.client_pid;
        next_task_id.store(other.next_task_id.load());
        
        // Invalidate the moved-from object
        other.shm_ptr = nullptr;
        other.task_queue = nullptr;
        other.shm_fd = -1;
    }
    return *this;
}

template <typename K, typename V>
bool KVSClient<K, V>::submit_task(const Task<K, V>& task) {
    if (!is_connected()) {
        return false;
    }
    
    // Use try_push for non-blocking submission
    // Returns false if queue is full
    return task_queue->try_push(task);
}

template <typename K, typename V>
int KVSClient<K, V>::get(const K& key) {
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

template <typename K, typename V>
int KVSClient<K, V>::set(const K& key, const V& value) {
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

template <typename K, typename V>
int KVSClient<K, V>::post(const K& key, const V& value) {
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

template <typename K, typename V>
int KVSClient<K, V>::del(const K& key) {
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
