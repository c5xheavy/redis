#include "server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <utility>

#include "connection.hpp"
#include "defines.hpp"
#include "executor.hpp"
#include "parser.hpp"

namespace redis {

server::server() : _epoll_fd{epoll_create1(0)} {
  if (_epoll_fd < 0) {
    std::perror("epoll_create1");
    std::exit(EXIT_FAILURE);
  }

  _server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (_server_fd < 0) {
    std::perror("socket");
    std::exit(EXIT_FAILURE);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): fcntl is a vararg by signature, there is no non-vararg alternative
  if (fcntl(_server_fd, F_SETFL, O_NONBLOCK) != 0) {
    std::perror("fcntl");
    std::exit(EXIT_FAILURE);
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  // NOLINTNEXTLINE(misc-include-cleaner): false positive — glibc defines these in bits/socket*.h; <sys/socket.h> is the real provider and is included
  if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::perror("setsockopt");
    std::exit(EXIT_FAILURE);
  }

  struct sockaddr_in server_addr {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(REDIS_PORT);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): canonical sockaddr idiom of the BSD socket API
  if (bind(_server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
    std::perror("bind");
    std::exit(EXIT_FAILURE);
  }

  const int connection_backlog = 5;
  if (listen(_server_fd, connection_backlog) != 0) {
    std::perror("listen");
    std::exit(EXIT_FAILURE);
  }

  _ev.events = EPOLLIN;
  _ev.data.fd = _server_fd;
  if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _server_fd, &_ev) != 0) {
    std::perror("epoll_ctl: _server_fd");
    std::exit(EXIT_FAILURE);
  }
}

server::~server() {
  if (close(_server_fd) != 0) {
    std::perror("close: server_fd");
    std::exit(EXIT_FAILURE);
  }
}

void server::serve() {
  struct sockaddr_in client_addr {};
  socklen_t client_addr_len = sizeof(client_addr);

  while (true) {
    int nfds = 0;
    do {
      nfds = epoll_wait(_epoll_fd, _events.data(), MAX_EVENTS, -1);
    } while (nfds < 0 && errno == EINTR);
    if (nfds < 0) {
      std::perror("epoll_wait");
      std::exit(EXIT_FAILURE);
    }

    assert(nfds >= 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(nfds); ++i) {
      if (_events.at(i).data.fd == _server_fd) {
        std::cout << "Connecting client...\n";
        int client_fd = -1;
        do {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): canonical sockaddr idiom of the BSD socket API
          client_fd = accept4(_server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len, SOCK_NONBLOCK);
        } while (client_fd < 0 && errno == EINTR);
        if (client_fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          std::perror("accept");
          std::exit(EXIT_FAILURE);
        }
        if (client_fd < 0) {
          continue;
        }
        std::cout << "Client connected\n";

        [[maybe_unused]] auto try_emplace_rv =
            _connections.try_emplace(client_fd, std::make_pair(redis::connection{client_fd}, redis::parser{}));
        assert(try_emplace_rv.second);
        _ev.events = EPOLLIN;
        _ev.data.fd = client_fd;
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &_ev) != 0) {
          std::perror("epoll_ctl: _client_fd");
          std::exit(EXIT_FAILURE);
        }
      } else {
        const int client_fd = _events.at(i).data.fd;

        read_input(client_fd);

        if (_connections.find(client_fd) == _connections.end()) {
          continue;
        }

        send_output(client_fd);
      }
    }
  }
}

ssize_t server::read_input(int client_fd) {
  //TODO(amir): multithreading
  static std::array<char, RECV_BUF_MAX_SIZE> recv_buf{};
  ssize_t bytes_recv = -1;
  do {
    bytes_recv = recv(client_fd, recv_buf.data(), sizeof(recv_buf), 0);
  } while (bytes_recv < 0 && errno == EINTR);
  if (bytes_recv < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      std::cout << "Closing client " << client_fd << " after failed recv\n";
      _connections.erase(client_fd);
    }
    return bytes_recv;
  }
  if (bytes_recv == 0) {
    std::cout << "Client " << client_fd << " closed connection\n";
    _connections.erase(client_fd);
    return bytes_recv;
  }

  auto& [connection, parser] = _connections.at(client_fd);
  assert(bytes_recv >= 0);
  connection.append_input_buffer(recv_buf, static_cast<std::size_t>(bytes_recv));
  parser.parse_input(connection);
  while (parser.has_command()) {
    connection.append_output_buffer(redis::executor::execute(parser.get_command()));
  }
  return bytes_recv;
}

ssize_t server::send_output(int client_fd) {
  epoll_event ev{};
  auto& [connection, parser] = _connections.at(client_fd);

  const std::span<const char> span = connection.get_bytes_for_send();
  if (!span.empty()) {
    ssize_t bytes_send = -1;
    do {
      bytes_send = send(client_fd, span.data(), span.size(), 0);
    } while (bytes_send < 0 && errno == EINTR);
    if (bytes_send < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        std::cout << "Closing client " << client_fd << " after failed send\n";
        _connections.erase(client_fd);
      } else {
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = client_fd;
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) != 0) {
          std::perror("epoll_ctl: client_fd");
          std::exit(EXIT_FAILURE);
        }
      }
      return bytes_send;
    }
    assert(bytes_send >= 0);
    connection.erase_bytes_after_send(static_cast<std::size_t>(bytes_send));
    if (connection.get_bytes_for_send().empty()) {
      ev.events = EPOLLIN;
      ev.data.fd = client_fd;
      if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, client_fd, &ev) != 0) {
        std::perror("epoll_ctl: client_fd");
        std::exit(EXIT_FAILURE);
      }
    }
    return bytes_send;
  }
  return 0;
}

}  // namespace redis
