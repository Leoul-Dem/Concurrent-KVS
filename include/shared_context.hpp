// Shared memory context containing both request queue and response table
#pragma once

#include "task_queue.hpp"
#include "response_table.hpp"

template <typename K, typename V>
struct SharedMemoryContext {
    TaskQueue<K, V> task_queue;
    ResponseTable<V> response_table;
    
    SharedMemoryContext() : task_queue(), response_table() {}
};
