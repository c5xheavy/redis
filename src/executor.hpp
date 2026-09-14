#ifndef MY_REDIS_SRC_EXECUTOR_HPP
#define MY_REDIS_SRC_EXECUTOR_HPP

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace redis {

class executor {
public:
  //TODO(amir): singleton
  static std::string execute(const std::vector<std::string>& command) {
    assert(command.size() == 1);
    assert(command[0] == "ping");
    return "+PONG\r\n";
  }

private:
  //TODO(amir): state
};

}  // namespace redis

#endif  // MY_REDIS_SRC_EXECUTOR_HPP
