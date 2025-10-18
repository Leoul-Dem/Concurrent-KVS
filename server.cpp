#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "concurrent_hashmap.cpp"
#include <iostream>
#include <vector>
#include <atomic>
#include <csignal>

volatile int curr_using = 0;
std::atomic<bool> quit = false;

std::vector<int> pid;

const char* socket_path = "/tmp/simple_socket";

void handle_sigint(int){
    for(auto i : pid){
        kill(i, SIGTERM);
    }
    quit = true;
}

void handle_resizing(){

}

int create_server_fd_and_listen(int& server_fd, std::string& err){
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        err = "socket";
        return -1;
    }

    // Remove any existing socket file
    unlink(socket_path);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Bind socket to a file path
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        err = "bind";
        return -1;
    }

    if (listen(server_fd, 5) == -1) {
        err = "listen";
        return -1;
    }

}

int accept_client_conn(int server_fd, std::vector<int>& client_fd){
    int fd = accept(server_fd, nullptr, nullptr);

    if (fd == -1) {
        perror("accept");
        return -1;
    }else{
        client_fd.push_back(fd);
        curr_using++;
    }
    return 1;
}

int exchange_pid_with_shmem_fd(int client_fd, int& value){
    int shmem_fd = 1234;

    read(client_fd, &value, sizeof(value));

    write(client_fd, &shmem_fd, sizeof(shmem_fd));

    return 1;
}



int main() {
    ConcurrentHashMap<int, int> map;

    signal(SIGINT, handle_sigint);

    // Create socket
    int server_fd;
    std::string error;

    create_server_fd_and_listen(server_fd, error);


    std::cout << "Server listening on " << socket_path << "\n";
    std::vector<int>client_fd;

    while (!quit) {
        if(accept_client_conn(server_fd, client_fd)){
           break;
        }

        int client_pid;
        exchange_pid_with_shmem_fd(client_fd.back(), client_pid);

        pid.push_back(client_pid);
    }



    close(server_fd);
    unlink(socket_path);



    return 0;
}


