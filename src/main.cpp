#include <array>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

constexpr size_t max_events = 10;

class connection {
private:
  class unique_fd {
  public:
    explicit unique_fd(int fd) : _fd{fd} {}

    ~unique_fd() {
      if (_fd == -1) return;
      close_fd();
    }

    unique_fd(unique_fd&& other) noexcept : _fd{std::exchange(other._fd, -1)} {}

    unique_fd& operator=(unique_fd&& other) noexcept {
      if (this == &other) return *this;
      if (_fd != -1) close_fd();
      _fd = std::exchange(other._fd, -1);
      return *this;
    }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

  private:
    void close_fd() noexcept {
      if (close(_fd) != 0 && errno != EINTR) {
        perror("close: unique_fd");
        abort();
      }
    }

    int _fd;
  };

public:
  explicit connection(int client_fd) : _client_fd{client_fd} {}

  ~connection() = default;

  connection(connection&&) noexcept = default;
  connection& operator=(connection&&) noexcept = default;

  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

private:
  unique_fd _client_fd;
  std::deque<char> _input_buffer;
  std::deque<char> _output_buffer;
};

static_assert(std::is_nothrow_move_constructible_v<connection>);
static_assert(std::is_nothrow_move_assignable_v<connection>);

int main() {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  signal(SIGPIPE, SIG_IGN);

  std::map<int, connection> connections;
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
      if (events[i].data.fd == server_fd) {
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

        auto try_emplace_rv = connections.try_emplace(client_fd, client_fd); // (int, connection(int))
        assert(try_emplace_rv.second);
        ev.events = EPOLLIN;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev)) {
          perror("epoll_ctl: client_fd");
          exit(EXIT_FAILURE);
        }
      } else {
        char recv_buf[1024];

        const char* pong_msg = "+PONG\r\n";
        size_t pong_msg_len = strlen(pong_msg);

        int client_fd = events[i].data.fd;
        ssize_t bytes_recv;
        do {
          bytes_recv = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        } while (bytes_recv < 0 && errno == EINTR);
        if (bytes_recv < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          std::cout << "Closing client " << client_fd << " after failed recv\n";
          connections.erase(client_fd);
          continue;
        }
        if (bytes_recv == 0) {
          std::cout << "Client " << client_fd << " closed connection\n";
          connections.erase(client_fd);
          continue;
        }
        ssize_t bytes_send;
        do {
          bytes_send = send(client_fd, pong_msg, pong_msg_len, 0);
        } while (bytes_send < 0 && errno == EINTR);
        if (bytes_send < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          std::cout << "Closing client " << client_fd << " after failed send\n";
          connections.erase(client_fd);
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
