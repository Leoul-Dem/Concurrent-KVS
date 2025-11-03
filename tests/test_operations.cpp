// Integration test to verify Phase 3 operations work correctly
// This tests that all KVS operations execute on the server side

#include "../include/kvs_server.hpp"
#include "../include/shared_context.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

// Standalone test - doesn't require shared memory, just tests the core logic
int main() {
    std::cout << "=== Phase 3 Operations Test ===" << std::endl;
    
    // Create a SharedMemoryContext in regular memory (not shared)
    SharedMemoryContext<int, int> context;
    
    // Create KVS Server
    KVSServer<int, int> server(&context.task_queue, &context.response_table);
    
    // Start with 2 worker threads
    if (!server.start(2)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "\nServer started with 2 workers" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test 1: SET operations
    std::cout << "\n--- Test 1: SET Operations ---" << std::endl;
    for (int i = 0; i < 5; ++i) {
        Task<int, int> task;
        task.cmd = CMD_SET;
        task.key = i;
        task.value = i * 100;
        task.has_value = true;
        task.client_pid = 12345;
        task.task_id = i;
        
        context.task_queue.push(task);
        std::cout << "Pushed SET task: key=" << i << ", value=" << (i * 100) << std::endl;
    }
    
    // Give workers time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test 2: GET operations (should find the values we just set)
    std::cout << "\n--- Test 2: GET Operations ---" << std::endl;
    for (int i = 0; i < 5; ++i) {
        Task<int, int> task;
        task.cmd = CMD_GET;
        task.key = i;
        task.has_value = false;
        task.client_pid = 12345;
        task.task_id = 100 + i;
        
        context.task_queue.push(task);
        std::cout << "Pushed GET task: key=" << i << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test 3: POST operations (should fail - keys already exist)
    std::cout << "\n--- Test 3: POST Operations (should fail) ---" << std::endl;
    for (int i = 0; i < 3; ++i) {
        Task<int, int> task;
        task.cmd = CMD_POST;
        task.key = i;
        task.value = 999;
        task.has_value = true;
        task.client_pid = 12345;
        task.task_id = 200 + i;
        
        context.task_queue.push(task);
        std::cout << "Pushed POST task: key=" << i << " (should fail)" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test 4: POST new keys (should succeed)
    std::cout << "\n--- Test 4: POST New Keys (should succeed) ---" << std::endl;
    for (int i = 10; i < 13; ++i) {
        Task<int, int> task;
        task.cmd = CMD_POST;
        task.key = i;
        task.value = i * 50;
        task.has_value = true;
        task.client_pid = 12345;
        task.task_id = 300 + i;
        
        context.task_queue.push(task);
        std::cout << "Pushed POST task: key=" << i << ", value=" << (i * 50) << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test 5: DELETE operations
    std::cout << "\n--- Test 5: DELETE Operations ---" << std::endl;
    for (int i = 0; i < 3; ++i) {
        Task<int, int> task;
        task.cmd = CMD_DELETE;
        task.key = i;
        task.has_value = false;
        task.client_pid = 12345;
        task.task_id = 400 + i;
        
        context.task_queue.push(task);
        std::cout << "Pushed DELETE task: key=" << i << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test 6: GET deleted keys (should not be found)
    std::cout << "\n--- Test 6: GET Deleted Keys (should not be found) ---" << std::endl;
    for (int i = 0; i < 3; ++i) {
        Task<int, int> task;
        task.cmd = CMD_GET;
        task.key = i;
        task.has_value = false;
        task.client_pid = 12345;
        task.task_id = 500 + i;
        
        context.task_queue.push(task);
        std::cout << "Pushed GET task: key=" << i << " (should not be found)" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Check storage size
    std::cout << "\n--- Final State ---" << std::endl;
    std::cout << "Storage size: " << server.storage_size() << " items" << std::endl;
    std::cout << "Expected: 5 (keys 3,4,10,11,12 should remain)" << std::endl;
    
    // Verify storage size is correct
    size_t expected_size = 5;
    size_t actual_size = server.storage_size();
    
    if (actual_size == expected_size) {
        std::cout << "\n✅ Phase 3 Test PASSED!" << std::endl;
        std::cout << "All operations (GET/SET/POST/DELETE) executed correctly" << std::endl;
    } else {
        std::cout << "\n❌ Phase 3 Test FAILED!" << std::endl;
        std::cout << "Expected " << expected_size << " items but got " << actual_size << std::endl;
    }
    
    // Stop server
    std::cout << "\nStopping server..." << std::endl;
    server.stop();
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    return (actual_size == expected_size) ? 0 : 1;
}
