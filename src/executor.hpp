#ifndef MY_REDIS_SRC_EXECUTOR_HPP
#define MY_REDIS_SRC_EXECUTOR_HPP

#include <string>
#include <vector>

namespace redis {

class executor {
public:
  //TODO(amir): singleton
  static std::string execute(const std::vector<std::string>& command);

private:
  static std::string str_tolower(std::string s);

  //TODO(amir): state
};

}  // namespace redis

#endif  // MY_REDIS_SRC_EXECUTOR_HPP
