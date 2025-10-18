#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <csignal>

volatile int paused = 0;
volatile int terminated = 0;

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
        return -1;
    }

    return 1;
}

int disconnect_from_server(int client_fd){
    close(client_fd);
    return 0;
}

int exchange_pid_with_shmem_fd(int client_fd, int& mem_fd){
    int pid = getpid();

    write(client_fd, &pid, sizeof(pid));

    read(client_fd, &mem_fd, sizeof(mem_fd));

    return 1;
}

int main() {

    signal(SIGUSR1, handle_sigusr1);
    signal(SIGUSR2, handle_sigusr2);
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigint);



    int client_fd;
    int mem_fd;

    if (connect_to_server(client_fd) == -1) {
        return 1;
    }

    exchange_pid_with_shmem_fd(client_fd, mem_fd);

    while(!terminated){
        if(paused){
            while(!terminated && paused){
                usleep(100000);
            }
        }

        // do stuff with kvs
    }


    return disconnect_from_server(client_fd);
}
