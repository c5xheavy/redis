#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <deque>

constexpr size_t max_events = 10;

struct connection {
  int client_fd;
  std::deque<char> input_buffer;
  std::deque<char> output_buffer;
};

connection* create_connection(int client_fd) {
  connection* conn = new connection{};
  conn->client_fd = client_fd;
  return conn;
}

int close_client(int client_fd) {
  int rv = close(client_fd);
  if (rv != 0) {
    perror("close: client_fd");
    exit(EXIT_FAILURE);
  }
  return rv;
}

int close_connection(connection* conn) {
  int rv = close_client(conn->client_fd);
  delete conn;
  return rv;
}

int main() {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  signal(SIGPIPE, SIG_IGN);

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
  ev.data.ptr = &server_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) != 0) {
    perror("epoll_ctl: server_fd");
    exit(EXIT_FAILURE);
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  while (true) {
    int nfds;
    do {
      nfds = epoll_wait(epoll_fd, events, max_events, -1);
    } while (nfds < 0 && errno == EINTR);
    if (nfds < 0) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nfds; ++i) {
      if (*static_cast<int*>(events[i].data.ptr) == server_fd) {
        std::cout << "Connecting client...\n";
        int client_fd;
        do {
          client_fd = accept4(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len, SOCK_NONBLOCK);
        } while (client_fd < 0 && errno == EINTR);
        if (client_fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        if (client_fd < 0) {
          continue;
        }
        std::cout << "Client connected\n";

        ev.events = EPOLLIN;
        ev.data.ptr = create_connection(client_fd);
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, static_cast<connection*>(ev.data.ptr)->client_fd, &ev)) {
          perror("epoll_ctl: client_fd");
          exit(EXIT_FAILURE);
        }
      } else {
        char recv_buf[1024];

        const char* pong_msg = "+PONG\r\n";
        size_t pong_msg_len = strlen(pong_msg);

        connection* conn = static_cast<connection*>(events[i].data.ptr);
        int client_fd = conn->client_fd;
        ssize_t bytes_recv;
        do {
          bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        } while (bytes_recv < 0 && errno == EINTR);
        if (bytes_recv < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          close_connection(conn);
          std::cout << "Closing client after failed recv\n";
          continue;
        }
        if (bytes_recv == 0) {
          close_connection(conn);
          std::cout << "Client closed connection\n";
          continue;
        }
        ssize_t bytes_send;
        do {
          bytes_send = send(client_fd, pong_msg, pong_msg_len, 0);
        } while (bytes_send < 0 && errno == EINTR);
        if (bytes_send < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          close_connection(conn);
          std::cout << "Closing client after failed send\n";
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
