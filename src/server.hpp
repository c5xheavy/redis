#ifndef MY_REDIS_SRC_SERVER_HPP
#define MY_REDIS_SRC_SERVER_HPP

#include <sys/types.h>

#include <map>
#include <utility>

#include "connection.hpp"
#include "parser.hpp"
#include "unique_fd.hpp"

namespace redis {

class server {
public:
  //TODO(amir): singleton

  server();
  ~server() = default;

  server(const server&) = delete;
  server& operator=(const server&) = delete;

  server(server&&) = delete;
  server& operator=(server&&) = delete;

  [[noreturn]] void serve();

  ssize_t read_input(int client_fd);

  ssize_t send_output(int client_fd);

private:
  std::map<int, std::pair<redis::connection, redis::parser>> _connections;
  unique_fd _epoll_fd;
  unique_fd _server_fd;
};

}  // namespace redis

#endif  // MY_REDIS_SRC_SERVER_HPP
