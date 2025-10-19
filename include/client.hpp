// Client-side networking interface declarations
#pragma once

#include <csignal>

// Signal state flags managed by the client process
extern volatile sig_atomic_t paused;
extern volatile sig_atomic_t terminated;

// Signal handlers toggling the client lifecycle states
void handle_sigusr1(int signal);
void handle_sigusr2(int signal);
void handle_sigterm(int signal);
void handle_sigint(int signal);

// Core client/server coordination helpers
int connect_to_server(int& client_fd);
int disconnect_from_server(int client_fd);
int exchange_pid_with_shmem_fd(int client_fd, int& mem_fd);
