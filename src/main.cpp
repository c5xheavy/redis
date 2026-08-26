#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

constexpr size_t max_events = 10;

int main() {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  epoll_event ev, events[max_events];

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  if (fcntl(server_fd, F_SETFL, O_NONBLOCK) != 0) {
    perror("fcntl");
    exit(EXIT_FAILURE);
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  ev.events = EPOLLIN;
  ev.data.fd = server_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) != 0) {
    perror("epoll_ctl: server_fd");
    exit(EXIT_FAILURE);
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  while (true) {
    int nfds = epoll_wait(epoll_fd, events, max_events, -1);
    if (nfds < 0) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nfds; ++i) {
      if (events[i].data.fd == server_fd) {
        std::cout << "Connecting client...\n";
        int client_fd = accept4(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len, SOCK_NONBLOCK);
        if (client_fd == -1) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        std::cout << "Client connected\n";

        ev.events = EPOLLIN;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev)) {
          perror("epoll_ctl: client_fd");
          exit(EXIT_FAILURE);
        }
      } else {
        char recv_buf[1024];
        ssize_t bytes_recv;

        const char* pong_msg = "+PONG\r\n";
        size_t pong_msg_len = strlen(pong_msg);

        int client_fd = events[i].data.fd;
        while ((bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0) {
          ssize_t bytes_send = send(client_fd, pong_msg, pong_msg_len, 0);
          if (bytes_send < 0) {
            perror("send");
            exit(EXIT_FAILURE);
          }
        }
        if (bytes_recv < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("recv");
          exit(EXIT_FAILURE);
        }
        if (bytes_recv == 0) {
          if (close(client_fd) != 0) {
            perror("close: client_fd");
            exit(EXIT_FAILURE);
          }
          std::cout << "Client closed connection\n";
        }
      }
    }
  }
 
  if (close(server_fd) != 0) {
    perror("close: server_fd");
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
}
