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
    assert(command.size() == 1);
    assert(command[0] == "ping");
    return "+PONG\r\n";
  }

private:
  //TODO(amir): state
};

}  // namespace redis

#endif  // MY_REDIS_SRC_EXECUTOR_HPP
