// KVS Server Library - Implementation (header-only for templates)
#pragma once

#include "kvs_server.hpp"
#include <iostream>
#include <chrono>
#include <thread>

template <typename K, typename V>
KVSServer<K, V>::KVSServer(TaskQueue<K, V>* queue, ResponseTable<V>* responses, size_t stripe_count)
    : storage(stripe_count)
    , task_queue(queue)
    , response_table(responses)
    , running(false)
{
    if (task_queue == nullptr) {
        throw std::invalid_argument("TaskQueue pointer cannot be null");
    }
    if (response_table == nullptr) {
        throw std::invalid_argument("ResponseTable pointer cannot be null");
    }
}

template <typename K, typename V>
KVSServer<K, V>::~KVSServer() {
    stop();
}

template <typename K, typename V>
bool KVSServer<K, V>::start(size_t num_threads) {
    // Check if already running
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        std::cerr << "KVSServer already running" << std::endl;
        return false;
    }
    
    // Spawn worker threads
    std::cout << "Starting KVSServer with " << num_threads << " worker threads..." << std::endl;
    
    workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(&KVSServer::worker_thread, this);
    }
    
    std::cout << "KVSServer started successfully" << std::endl;
    return true;
}

template <typename K, typename V>
void KVSServer<K, V>::stop() {
    // Signal workers to stop
    if (!running.exchange(false)) {
        return; // Already stopped
    }
    
    std::cout << "Stopping KVSServer..." << std::endl;
    
    // Wait for all workers to finish
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers.clear();
    std::cout << "KVSServer stopped" << std::endl;
}

template <typename K, typename V>
bool KVSServer<K, V>::is_running() const {
    return running.load(std::memory_order_acquire);
}

template <typename K, typename V>
size_t KVSServer<K, V>::worker_count() const {
    return workers.size();
}

template <typename K, typename V>
size_t KVSServer<K, V>::storage_size() const {
    return storage.size();
}

template <typename K, typename V>
void KVSServer<K, V>::worker_thread() {
    std::cout << "Worker thread " << std::this_thread::get_id() << " started" << std::endl;
    
    Task<K, V> task;
    
    while (running.load(std::memory_order_acquire)) {
        // Try to pop a task from the queue (non-blocking)
        if (task_queue->try_pop(task, 100)) {
            // Process the task
            process_task(task);
        } else {
            // Queue is empty, sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    std::cout << "Worker thread " << std::this_thread::get_id() << " stopped" << std::endl;
}

template <typename K, typename V>
void KVSServer<K, V>::process_task(const Task<K, V>& task) {
    // Log task processing (can be made conditional/debug-only later)
    const char* cmd_name = "UNKNOWN";
    switch (task.cmd) {
        case CMD_GET:    cmd_name = "GET";    break;
        case CMD_SET:    cmd_name = "SET";    break;
        case CMD_POST:   cmd_name = "POST";   break;
        case CMD_DELETE: cmd_name = "DELETE"; break;
    }
    
    std::cout << "[Worker " << std::this_thread::get_id() << "] "
              << "Processing " << cmd_name 
              << " (client_pid=" << task.client_pid 
              << ", task_id=" << task.task_id << ")" << std::endl;
    
    // Execute the operation
    switch (task.cmd) {
        case CMD_GET: {
            V value;
            bool found = storage.find(task.key, value);
            
            // Write response
            Response<V>* response = response_table->get_slot(task.task_id);
            if (found) {
                response->value = value;
                response->status.store(RESPONSE_SUCCESS, std::memory_order_release);
                std::cout << "  GET key=" << task.key << " -> value=" << value << std::endl;
            } else {
                response->status.store(RESPONSE_NOT_FOUND, std::memory_order_release);
                std::cout << "  GET key=" << task.key << " -> NOT FOUND" << std::endl;
            }
            break;
        }
        
        case CMD_SET: {
            storage.insert_or_assign(task.key, task.value);
            
            // Write response
            Response<V>* response = response_table->get_slot(task.task_id);
            response->status.store(RESPONSE_SUCCESS, std::memory_order_release);
            std::cout << "  SET key=" << task.key << ", value=" << task.value << std::endl;
            break;
        }
        
        case CMD_POST: {
            bool inserted = storage.insert(task.key, task.value);
            
            // Write response
            Response<V>* response = response_table->get_slot(task.task_id);
            if (inserted) {
                response->status.store(RESPONSE_SUCCESS, std::memory_order_release);
                std::cout << "  POST key=" << task.key << ", value=" << task.value << " -> SUCCESS" << std::endl;
            } else {
                response->status.store(RESPONSE_FAILED, std::memory_order_release);
                std::cout << "  POST key=" << task.key << " -> FAILED (already exists)" << std::endl;
            }
            break;
        }
        
        case CMD_DELETE: {
            bool deleted = storage.erase(task.key);
            
            // Write response
            Response<V>* response = response_table->get_slot(task.task_id);
            if (deleted) {
                response->status.store(RESPONSE_SUCCESS, std::memory_order_release);
                std::cout << "  DELETE key=" << task.key << " -> SUCCESS" << std::endl;
            } else {
                response->status.store(RESPONSE_NOT_FOUND, std::memory_order_release);
                std::cout << "  DELETE key=" << task.key << " -> NOT FOUND" << std::endl;
            }
            break;
        }
        
        default:
            std::cerr << "  ERROR: Unknown command " << task.cmd << std::endl;
            break;
    }
}
