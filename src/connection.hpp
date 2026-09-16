#ifndef MY_REDIS_SRC_CONNECTION_HPP
#define MY_REDIS_SRC_CONNECTION_HPP

#include <array>
#include <cstddef>
#include <deque>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "defines.hpp"
#include "unique_fd.hpp"

namespace redis {

class connection {
public:
  explicit connection(int client_fd) : _client_fd{client_fd} {}

  ~connection() = default;

  connection(connection&&) noexcept = default;
  connection& operator=(connection&&) noexcept = default;

  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  std::size_t append_input_buffer(const std::array<char, RECV_BUF_MAX_SIZE>& recv_buf, std::size_t bytes_recv);

  [[nodiscard]] bool has_bytes(std::size_t n) const;

  [[nodiscard]] std::string read_bytes(std::size_t n);

  [[nodiscard]] bool has_str() const;

  [[nodiscard]] std::string read_str();

  void append_output_buffer(const std::span<const char>& span);

  [[nodiscard]] std::span<const char> get_bytes_for_send() const;

  void erase_bytes_after_send(std::size_t n);

private:
  unique_fd _client_fd;
  std::deque<char> _input_buffer;
  std::vector<char> _output_buffer;
  std::size_t _offset = 0;
};

static_assert(std::is_nothrow_move_constructible_v<connection>);
static_assert(std::is_nothrow_move_assignable_v<connection>);

}  // namespace redis

#endif  // MY_REDIS_SRC_CONNECTION_HPP
