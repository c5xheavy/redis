#include "executor.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace redis {

std::string executor::execute(const std::vector<std::string>& command) {
  if (command.size() == 1 && str_tolower(command[0]) == "ping") {
    return "+PONG\r\n";
  }
  return "-ERR unknown command\r\n";
}

std::string executor::str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace redis
