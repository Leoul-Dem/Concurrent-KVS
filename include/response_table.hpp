// Response table for returning results from server to clients
#pragma once

#include <atomic>
#include <cstddef>

// Response status codes
inline constexpr int RESPONSE_PENDING = 0;
inline constexpr int RESPONSE_SUCCESS = 1;
inline constexpr int RESPONSE_NOT_FOUND = 2;
inline constexpr int RESPONSE_FAILED = 3;

template <typename V>
struct Response {
    std::atomic<int> status{RESPONSE_PENDING};
    V value;
    
    // Reset for reuse
    void reset() {
        status.store(RESPONSE_PENDING, std::memory_order_release);
    }
    
    // Check if completed
    bool is_completed() const {
        return status.load(std::memory_order_acquire) != RESPONSE_PENDING;
    }
};

// Fixed-size response table in shared memory
// Clients use task_id % TABLE_SIZE to find their response slot
template <typename V>
class ResponseTable {
private:
    static constexpr std::size_t TABLE_SIZE = 1024;
    alignas(64) Response<V> responses[TABLE_SIZE];
    
public:
    ResponseTable() = default;
    
    // Get response slot for a task_id
    Response<V>* get_slot(int task_id) {
        return &responses[task_id % TABLE_SIZE];
    }
    
    // Clear a response slot (client should call after reading)
    void clear_slot(int task_id) {
        responses[task_id % TABLE_SIZE].reset();
    }
    
    // Get the table size
    static constexpr std::size_t size() {
        return TABLE_SIZE;
    }
};
