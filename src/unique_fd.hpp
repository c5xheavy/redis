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
  unique_fd() = default;
  explicit unique_fd(int fd) : _fd{fd} {}

  ~unique_fd() {
    if (_fd != -1) {
      close_fd();
    }
  }

  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;

  unique_fd(unique_fd&& other) noexcept : unique_fd() {
    swap(*this, other);
  }

  unique_fd& operator=(unique_fd&& other) noexcept {
    unique_fd tmp{std::move(other)};
    swap(*this, tmp);
    return *this;
  }

  friend void swap(unique_fd& first, unique_fd& second) noexcept {
    using std::swap;
    swap(first._fd, second._fd);
  }

  [[nodiscard]] int native_handle() const noexcept {
    return _fd;
  }

  void close_fd() noexcept {
    if (close(_fd) != 0) {
      const int close_errno = errno;
      if (close_errno != EINTR) {
        std::perror("close: unique_fd");
      }
      if (close_errno == EBADF) {
        std::abort();
      }
    }
    _fd = -1;
  }

private:
  int _fd = -1;
};

}  // namespace redis

#endif  // MY_REDIS_SRC_UNIQUE_FD_HPP
