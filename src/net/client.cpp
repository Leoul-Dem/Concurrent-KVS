#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <csignal>
#include <errno.h>

#include "../../include/kvs_client.hpp"

volatile sig_atomic_t paused = 0;
volatile sig_atomic_t terminated = 0;

void handle_sigusr1(int) {
    paused = 1;
}

void handle_sigusr2(int) {
    paused = 0;
}

void handle_sigterm(int){
    terminated = 1;
}

void handle_sigint(int){
    terminated = 1;
}

int connect_to_server(int& client_fd){
    const char* socket_path = "/tmp/simple_socket";
    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("connect_to_server failure: socket");
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(client_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect_to_server failure: connect");
        close(client_fd);
        return -1;
    }

    return 1;
}

ssize_t read_full(int fd, void* buf, size_t count) {
    size_t total_read = 0;
    char* ptr = (char*)buf;

    while (total_read < count) {
        ssize_t n = read(fd, ptr + total_read, count - total_read);

        if (n == -1) {
            if (errno == EINTR) {
                // Interrupted by signal, check if we should terminate
                if (terminated) return -1;
                continue;  // Retry
            }
            perror("read error");
            return -1;  // Real error
        }

        if (n == 0) {
            // Connection closed
            std::cerr << "Connection closed by server" << std::endl;
            return -1;
        }

        total_read += n;
    }

    return total_read;
}

ssize_t write_full(int fd, const void* buf, size_t count) {
    size_t total_written = 0;
    const char* ptr = (const char*)buf;

    while (total_written < count) {
        ssize_t n = write(fd, ptr + total_written, count - total_written);

        if (n == -1) {
            if (errno == EINTR) {
                if (terminated) return -1;
                continue;  // Retry
            }
            perror("write error");
            return -1;
        }

        total_written += n;
    }

    return total_written;
}

int exchange_pid_with_shmem_fd(int client_fd, int& mem_fd){
    int pid = getpid();

    if (write_full(client_fd, &pid, sizeof(pid)) == -1) {
        std::cerr << "Failed to send PID to server" << std::endl;
        return -1;
    }

    if (read_full(client_fd, &mem_fd, sizeof(mem_fd)) == -1) {
        if (terminated) {
            std::cerr << "Terminated while waiting for server response" << std::endl;
        } else {
            std::cerr << "Failed to receive shmem_fd from server" << std::endl;
        }
        return -1;
    }

    return pid;
}

int run_client(){
  signal(SIGUSR1, handle_sigusr1);
  signal(SIGUSR2, handle_sigusr2);
  signal(SIGTERM, handle_sigterm);
  signal(SIGINT, handle_sigint);

  int client_fd;

  if (connect_to_server(client_fd) == -1) {
      return 1;
  }

  int mem_fd;

  int pid = exchange_pid_with_shmem_fd(client_fd, mem_fd);

  if (pid == -1) {
      close(client_fd);
      return 1;
  }

  std::cout << "SHMEM: " << mem_fd << std::endl;
  std::cout << "PID: " << pid << std::endl;

  // Map shared memory and create KVS client library instance
  try {
      KVSClient<int, int> kvs_client(mem_fd, pid);
      
      std::cout << "Successfully connected to shared memory task queue" << std::endl;
      std::cout << "Queue size: " << kvs_client.queue_size() << std::endl;

      // Main event loop
      int operation_count = 0;
      while(!terminated){
        if(paused){
            while(!terminated && paused){
                usleep(100000);  // 100ms - reasonable for this use case
            }
        }

        // Demo: Submit some test operations
        if (operation_count < 10) {
            // Phase 1: SET operations (keys 0-9)
            int task_id = kvs_client.set(operation_count, operation_count * 100);
            if (task_id != -1) {
                std::cout << "Submitted SET operation: key=" << operation_count 
                          << ", value=" << (operation_count * 100) 
                          << ", task_id=" << task_id << std::endl;
            } else {
                std::cerr << "Failed to submit task (queue full?)" << std::endl;
            }
            
            operation_count++;
            usleep(500000); // Sleep 500ms between operations
        } else if (operation_count < 20) {
            // Phase 2: GET operations (read back keys 0-9)
            int key = operation_count - 10;
            int task_id = kvs_client.get(key);
            if (task_id != -1) {
                std::cout << "Submitted GET operation: key=" << key 
                          << ", task_id=" << task_id << std::endl;
            } else {
                std::cerr << "Failed to submit GET task" << std::endl;
            }
            
            operation_count++;
            usleep(500000);
        } else if (operation_count < 25) {
            // Phase 3: POST operations (try to insert existing keys - should fail)
            int key = operation_count - 20;
            int task_id = kvs_client.post(key, 999);
            if (task_id != -1) {
                std::cout << "Submitted POST operation: key=" << key 
                          << ", value=999, task_id=" << task_id 
                          << " (should FAIL - key exists)" << std::endl;
            } else {
                std::cerr << "Failed to submit POST task" << std::endl;
            }
            
            operation_count++;
            usleep(500000);
        } else if (operation_count < 30) {
            // Phase 4: DELETE operations (remove keys 0-4)
            int key = operation_count - 25;
            int task_id = kvs_client.del(key);
            if (task_id != -1) {
                std::cout << "Submitted DELETE operation: key=" << key 
                          << ", task_id=" << task_id << std::endl;
            } else {
                std::cerr << "Failed to submit DELETE task" << std::endl;
            }
            
            operation_count++;
            usleep(500000);
        } else if (operation_count < 35) {
            // Phase 5: GET deleted keys (should not be found)
            int key = operation_count - 30;
            int task_id = kvs_client.get(key);
            if (task_id != -1) {
                std::cout << "Submitted GET operation: key=" << key 
                          << ", task_id=" << task_id 
                          << " (should NOT FOUND - was deleted)" << std::endl;
            } else {
                std::cerr << "Failed to submit GET task" << std::endl;
            }
            
            operation_count++;
            usleep(500000);
        } else {
            usleep(100000); // Just idle
        }
      }

      std::cout << "Client shutting down..." << std::endl;
      // KVSClient destructor will clean up shared memory mapping
      
  } catch (const std::exception& e) {
      std::cerr << "Error initializing KVS client: " << e.what() << std::endl;
      close(client_fd);
      return 1;
  }

  close(client_fd);
  return 0;
}