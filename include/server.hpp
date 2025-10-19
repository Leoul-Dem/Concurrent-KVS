// Server-side networking interface declarations
#pragma once

#include <atomic>
#include <string>
#include <vector>

// Shared server state exposed for coordination with other modules
extern volatile int curr_using;
extern std::atomic<bool> quit;
extern std::vector<int> pid;
extern const char* socket_path;

// Signal management
void handle_sigint(int signal);
void handle_resizing();

// Server lifecycle helpers
int create_server_fd_and_listen(int& server_fd, std::string& err);
int accept_client_conn(int server_fd, std::vector<int>& client_fd);
int exchange_pid_with_shmem_fd(int client_fd, int& value);
