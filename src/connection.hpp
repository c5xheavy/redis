#ifndef MY_REDIS_SRC_CONNECTION_HPP
#define MY_REDIS_SRC_CONNECTION_HPP

#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "defines.hpp"

namespace redis {

class connection {
private:
  class unique_fd {
  public:
    explicit unique_fd(int fd) : _fd{fd} {}

    ~unique_fd() {
      if (_fd != -1) {
        close_fd();
      }
    }

    unique_fd(unique_fd&& other) noexcept : _fd{std::exchange(other._fd, -1)} {}

    unique_fd& operator=(unique_fd&& other) noexcept {
      if (this == &other) {
        return *this;
      }
      if (_fd != -1) {
        close_fd();
      }
      _fd = std::exchange(other._fd, -1);
      return *this;
    }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

  private:
    void close_fd() noexcept {
      if (close(_fd) != 0 && errno != EINTR) {
        perror("close: unique_fd");
        std::abort();
      }
      _fd = -1;
    }

    int _fd;
  };

public:
  explicit connection(int client_fd) : _client_fd{client_fd} {}

  ~connection() = default;

  connection(connection&&) noexcept = default;
  connection& operator=(connection&&) noexcept = default;

  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  size_t append_input_buffer(const std::array<char, RECV_BUF_MAX_SIZE>& recv_buf, ssize_t bytes_recv);

  [[nodiscard]] bool has_bytes(size_t n) const;

  [[nodiscard]] std::string read_bytes(size_t n);

  [[nodiscard]] bool has_str() const;

  [[nodiscard]] std::string read_str();

  void append_output_buffer(const std::span<const char>& span);

  [[nodiscard]] std::span<const char> get_bytes_for_send() const;

  void erase_bytes_after_send(size_t n);

private:
  unique_fd _client_fd;
  std::deque<char> _input_buffer;
  std::vector<char> _output_buffer;
  size_t _offset = 0;
};

static_assert(std::is_nothrow_move_constructible_v<connection>);
static_assert(std::is_nothrow_move_assignable_v<connection>);

}  // namespace redis

#endif  // MY_REDIS_SRC_CONNECTION_HPP
