#include "parser.hpp"

#include <cassert>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "connection.hpp"

namespace redis {

bool parser::has_command() const {
  return !_commands.empty();
}

std::vector<std::string> parser::get_command() {
  assert(has_command());
  std::vector<std::string> command = std::move(_commands.front());
  _commands.pop();
  return command;
}

void parser::parse_input(connection& connection) {
  while (true) {
    switch (_state) {
      case state::expect_command: {
        assert(_args_expected == 0);
        if (!connection.has_str()) {
          return;
        }
        const std::string str = connection.read_str();
        if (str[0] != '*') {
          std::istringstream iss{str};
          _commands.emplace(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{});
          break;
        }
        _args_expected = from_chars(str, 1, str.size() - 2);
        if (_args_expected == 0) {
          break;
        }
        _state = state::expect_arg_len;
        break;
      }
      case state::expect_arg_len: {
        assert(_arg_len == 0);
        if (!connection.has_str()) {
          return;
        }
        const std::string str = connection.read_str();
        if (str[0] != '$') {
          throw std::invalid_argument("parse_str_len: expected str_len");
        }
        _arg_len = from_chars(str, 1, str.size() - 2);
        _state = state::expect_arg_payload;
        break;
      }
      case state::expect_arg_payload: {
        if (!connection.has_bytes(_arg_len + 2)) {
          return;
        }
        std::string str = connection.read_bytes(_arg_len + 2);
        assert(str[str.size() - 2] == '\r');
        assert(str[str.size() - 1] == '\n');
        str.pop_back();
        str.pop_back();
        _wip_command.push_back(std::move(str));
        _arg_len = 0;
        _state = state::expect_arg_len;
        if (_wip_command.size() == _args_expected) {
          _commands.push(std::move(_wip_command));
          _wip_command.clear();
          _args_expected = 0;
          _state = state::expect_command;
        }
        break;
      }
    }
  }
}

std::size_t parser::from_chars(const std::string& str, std::size_t first, std::size_t last) {
  assert(first < last);
  assert(last <= str.size());
  std::size_t value = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): std::from_chars takes pointer pairs, there is no span/range overload
  auto [ptr, ec] = std::from_chars(str.c_str() + first, str.c_str() + last, value);
  assert(ec == std::errc{});
  return value;
}

}  // namespace redis
