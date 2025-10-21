#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <csignal>
#include <vector>
#include <sys/select.h>
#include <errno.h>

const char* socket_path = "/tmp/simple_socket";
std::vector<int> pid;
volatile sig_atomic_t terminated = 0;

void handle_sigint(int){
  terminated = 1;
}

int create_server_fd_and_listen(int& server_fd){
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
      std::cerr << "socket" << std::endl;  
        return -1;
    }

    unlink(socket_path);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
      std::cerr << "bind" << std::endl;  
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) == -1) {
      std::cerr << "listen" << std::endl;  
        close(server_fd);
        return -1;
    }
    return 1;
}

int accept_client_conn(int server_fd, std::vector<int>& client_fd){
  // Use select() with timeout to check if accept() would block
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(server_fd, &readfds);
  
  struct timeval tv;
  tv.tv_sec = 1;  // 1 second timeout
  tv.tv_usec = 0;
  
  int ret = select(server_fd + 1, &readfds, nullptr, nullptr, &tv);
  
  if (ret == -1) {
      if (errno == EINTR) return 0;  // Interrupted by signal
      std::cerr << "select error" << std::endl;
      return -1;
  }
  
  if (ret == 0) {
      // Timeout - no client ready
      return 0;
  }
  
  // Socket is ready, accept won't block
  int new_fd = accept(server_fd, nullptr, nullptr);

  if (new_fd == -1) {
      if (errno == EINTR) return 0;
      std::cerr << "accept" << std::endl;  
      return -1;
  }
  
  client_fd.push_back(new_fd);
  return 1;
}

int main(){
  
  signal(SIGINT, handle_sigint);
  int server_fd;
  std::vector<int> client_fd;
  int shmem_fd = 1234;
  
  if (create_server_fd_and_listen(server_fd) == -1) {
      return 1;
  }
  
  std::cout << "Server listening on " << socket_path << std::endl;
  std::cout << "Press Ctrl+C to stop..." << std::endl;
  
  while(!terminated){
    int result = accept_client_conn(server_fd, client_fd);
    
    if (result <= 0) {
        continue;  // Timeout or error, check terminated flag
    }
    
    int new_pid;
    ssize_t bytes_read = read(client_fd.back(), &new_pid, sizeof(new_pid));
    
    if (bytes_read != sizeof(new_pid)) {
        std::cerr << "Failed to read PID" << std::endl;
        close(client_fd.back());
        client_fd.pop_back();
        continue;
    }
    
    pid.push_back(new_pid);
    
    ssize_t bytes_written = write(client_fd.back(), &shmem_fd, sizeof(shmem_fd));
    if (bytes_written != sizeof(shmem_fd)) {
        std::cerr << "Failed to write shmem_fd" << std::endl;
    }
    
    std::cout << "PID " << pid.size() << ": " << pid.back() << std::endl;
  }
  
  // Cleanup on termination
  std::cout << "\nShutting down..." << std::endl;
  for(auto p : pid){
      std::cout << "Killing PID: " << p << std::endl;
      kill(p, SIGTERM);
  }
  
  for(auto fd : client_fd){
      close(fd);
  }
  
  std::cout << "SHMEM: " << shmem_fd << std::endl; 
  close(server_fd);
  unlink(socket_path);
  
  return 0;
}