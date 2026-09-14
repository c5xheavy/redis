#ifndef MY_REDIS_SRC_CONNECTION_HPP
#define MY_REDIS_SRC_CONNECTION_HPP

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

  [[nodiscard]] size_t input_buffer_size() const {
    return _input_buffer.size();
  }

  size_t append_input_buffer(const std::array<char, RECV_BUF_MAX_SIZE>& recv_buf, ssize_t bytes_recv) {
    if (bytes_recv > 0) {
      _input_buffer.insert(_input_buffer.end(), recv_buf.begin(), recv_buf.begin() + bytes_recv);
    }
    return _input_buffer.size();
  }

  [[nodiscard]] bool has_bytes(size_t n) const {
    return _input_buffer.size() >= n;
  }

  [[nodiscard]] std::string read_bytes(size_t n) {
    assert(has_bytes(n));
    std::string res;
    while (n-- > 0) {
      res.push_back(_input_buffer.front());
      _input_buffer.pop_front();
    }
    return res;
  }

  [[nodiscard]] bool has_str() const {
    if (_input_buffer.size() < 2) {
      return false;
    }
    for (size_t i = 0; i < _input_buffer.size() - 1; ++i) {
      if (_input_buffer[i] == '\r' && _input_buffer[i + 1] == '\n') {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::string read_str() {
    assert(has_str());
    std::string res;
    while (!_input_buffer.empty() && (res.size() < 2 || res[res.size() - 2] != '\r' || res[res.size() - 1] != '\n')) {
      res.push_back(_input_buffer.front());
      _input_buffer.pop_front();
    }
    assert(res[res.size() - 2] == '\r');
    assert(res[res.size() - 1] == '\n');
    return res;
  }

  void append_output_buffer(const std::span<const char>& span) {
    _output_buffer.insert(_output_buffer.end(), span.begin(), span.end());
  }

  [[nodiscard]] std::span<const char> get_bytes_for_send() const {
    return std::span{_output_buffer}.subspan(_offset);
  }

  void erase_bytes_after_send(size_t n) {
    assert(_offset + n <= _output_buffer.size());
    _offset += n;
    if (_offset == _output_buffer.size()) {
      _output_buffer.clear();
      _offset = 0;
    }
  }

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
