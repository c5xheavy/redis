#ifndef MY_REDIS_SRC_EXECUTOR_HPP
#define MY_REDIS_SRC_EXECUTOR_HPP

#include <cassert>
#include <string>
#include <vector>

namespace redis {

class executor {
public:
  //TODO(amir): singleton
  static std::string execute([[maybe_unused]] const std::vector<std::string>& command) {
    if (command.size() == 1 && command[0] == "ping") {
      return "+PONG\r\n";
    }
    return "-ERR unknown command\r\n";
  }

private:
  //TODO(amir): state
};

}  // namespace redis

#endif  // MY_REDIS_SRC_EXECUTOR_HPP
