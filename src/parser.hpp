#ifndef MY_REDIS_SRC_PARSER_HPP
#define MY_REDIS_SRC_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#include "connection.hpp"

namespace redis {

class parser {
private:
  enum class state : std::uint8_t { expect_command, expect_arg_len, expect_arg_payload };

public:
  [[nodiscard]] bool has_command() const;

  [[nodiscard]] std::vector<std::string> get_command();

  void parse_input(connection& connection);

private:
  [[nodiscard]] static std::size_t from_chars(const std::string& str, std::size_t first, std::size_t last);

  std::queue<std::vector<std::string>> _commands;
  std::vector<std::string> _wip_command;
  state _state{state::expect_command};
  std::size_t _args_expected = 0;
  std::size_t _arg_len = 0;
};

}  // namespace redis

#endif  // MY_REDIS_SRC_PARSER_HPP
