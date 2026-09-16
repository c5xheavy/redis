#ifndef MY_REDIS_SRC_SERVER_HPP
#define MY_REDIS_SRC_SERVER_HPP

#include <sys/epoll.h>
#include <sys/types.h>

#include <array>
#include <map>
#include <utility>

#include "connection.hpp"
#include "defines.hpp"
#include "parser.hpp"

namespace redis {

class server {
public:
  //TODO(amir): singleton

  server();

  ~server();

  server(const server&) = delete;
  server& operator=(const server&) = delete;

  server(server&&) = delete;
  server& operator=(server&&) = delete;

  [[noreturn]] void serve();

  ssize_t read_input(int client_fd);

  ssize_t send_output(int client_fd);

private:
  std::map<int, std::pair<redis::connection, redis::parser>> _connections;
  epoll_event _ev{};
  std::array<epoll_event, MAX_EVENTS> _events{};
  int _epoll_fd;
  int _server_fd;
};

}  // namespace redis

#endif  // MY_REDIS_SRC_SERVER_HPP
