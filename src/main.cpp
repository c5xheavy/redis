#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

int main() {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt");
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    perror("bind");
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    perror("listen");
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect...\n";
  int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
  if (client_fd == -1) {
    perror("accept");
    return 1;
  }
  std::cout << "Client connected\n";

  char recv_buf[1024];
  ssize_t bytes_recv;

  const char* pong_msg = "+PONG\r\n";
  size_t pong_msg_len = strlen(pong_msg);

  while ((bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
    ssize_t bytes_send = send(client_fd, pong_msg, pong_msg_len, 0);
    if (bytes_send < 0) {
      perror("send");
      return 1;
    }
  }
  if (bytes_recv < 0) {
    perror("recv");
    return 1;
  }
 
  close(server_fd);

  return 0;
}
