#ifndef MY_REDIS_SRC_UNIQUE_FD_HPP
#define MY_REDIS_SRC_UNIQUE_FD_HPP

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace redis {

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

}  // namespace redis

#endif  // MY_REDIS_SRC_UNIQUE_FD_HPP
