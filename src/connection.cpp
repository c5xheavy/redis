#include "connection.hpp"

#include <sys/types.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <span>
#include <string>
#include <vector>

#include "defines.hpp"

namespace redis {

size_t connection::append_input_buffer(const std::array<char, RECV_BUF_MAX_SIZE>& recv_buf, ssize_t bytes_recv) {
  if (bytes_recv > 0) {
    _input_buffer.insert(_input_buffer.end(), recv_buf.begin(), recv_buf.begin() + bytes_recv);
  }
  return _input_buffer.size();
}

bool connection::has_bytes(size_t n) const {
  return _input_buffer.size() >= n;
}

std::string connection::read_bytes(size_t n) {
  assert(has_bytes(n));
  std::string res;
  while (n-- > 0) {
    res.push_back(_input_buffer.front());
    _input_buffer.pop_front();
  }
  return res;
}

bool connection::has_str() const {
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

std::string connection::read_str() {
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

void connection::append_output_buffer(const std::span<const char>& span) {
  _output_buffer.insert(_output_buffer.end(), span.begin(), span.end());
}

std::span<const char> connection::get_bytes_for_send() const {
  return std::span{_output_buffer}.subspan(_offset);
}

void connection::erase_bytes_after_send(size_t n) {
  assert(_offset + n <= _output_buffer.size());
  _offset += n;
  if (_offset == _output_buffer.size()) {
    _output_buffer.clear();
    _offset = 0;
  }
}

}  // namespace redis
