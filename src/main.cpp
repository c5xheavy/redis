#include <array>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <map>
#include <netinet/in.h>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

constexpr size_t REDIS_PORT = 6379;
constexpr size_t MAX_EVENTS = 10;
constexpr size_t RECV_BUF_MAX_SIZE = 1024;

class connection {
private:
  class unique_fd {
  public:
    explicit unique_fd(int fd) : _fd{fd} {}

    ~unique_fd() {
      if (_fd == -1) {
        return;
      }
      close_fd();
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

  size_t append(const std::array<char, RECV_BUF_MAX_SIZE>& recv_buf, ssize_t bytes_recv) {
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
    while(n-- > 0) {
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

private:
  unique_fd _client_fd;
  std::deque<char> _input_buffer;
  std::deque<char> _output_buffer;
};

static_assert(std::is_nothrow_move_constructible_v<connection>);
static_assert(std::is_nothrow_move_assignable_v<connection>);

class parser {
private:
  enum class state : std::uint8_t {
    expect_command,
    expect_arg_len,
    expect_arg_payload
  };

public:
  [[nodiscard]] bool has_command() const {
    return !commands.empty();
  }

  [[nodiscard]] std::vector<std::string> get_command() {
    assert(has_command());
    std::vector<std::string> command = std::move(commands.front());
    commands.pop();
    return command;
  }

  void parse_input(connection& conn) {
    while (true) {
      switch (_state) {
        case state::expect_command:
          {
            assert(args_expected == 0);
            if (!conn.has_str()) {
              return;
            }
            const std::string str = conn.read_str();
            if (str[0] != '*') {
              std::istringstream iss{str};
              commands.emplace(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{});
              break;
            }
            args_expected = from_chars(str, 1, str.size() - 2);
            if (args_expected == 0) {
              break;
            }
            _state = state::expect_arg_len;
            break;
          }
        case state::expect_arg_len:
          {
            assert(arg_len == 0);
            if (!conn.has_str()) {
              return;
            }
            const std::string str = conn.read_str();
            if (str[0] != '$') {
              throw std::invalid_argument("parse_str_len: expected str_len");
            }
            arg_len = from_chars(str, 1, str.size() - 2);
            _state = state::expect_arg_payload;
            break;
          }
        case state::expect_arg_payload:
          {
            if (!conn.has_bytes(arg_len + 2)) {
              return;
            }
            std::string str = conn.read_bytes(arg_len + 2);
            assert(str[str.size() - 2] == '\r');
            assert(str[str.size() - 1] == '\n');
            str.pop_back();
            str.pop_back();
            wip_command.push_back(std::move(str));
            arg_len = 0;
            _state = state::expect_arg_len;
            if (wip_command.size() == args_expected) {
              commands.push(std::move(wip_command));
              wip_command.clear();
              args_expected = 0;
              _state = state::expect_command;
            }
            break;
          }
      }
    }
  }

private:
  /*
  [[nodiscard]] bool parse_arr_len(connection& conn) {
    assert(arr_len == 0);
    if (!conn.has_str()) {
      return false;
    }
    const std::string str = conn.read_str();
    if (str[0] != '*') {
      throw std::invalid_argument("parse_arr_len: expected arr_len");
    }
    arr_len = from_chars(str, 1, str.size() - 2);
    return true;
  }

  [[nodiscard]] bool parse_str_len(connection& conn) {
    assert(str_len == 0);
    if (!conn.has_str()) {
      return false;
    }
    const std::string str = conn.read_str();
    if (str[0] != '$') {
      throw std::invalid_argument("parse_str_len: expected str_len");
    }
    str_len = from_chars(str, 1, str.size() - 2);
    return true;
  }
  */

  [[nodiscard]] static size_t from_chars(const std::string& str, size_t first, size_t last) {
    assert(first < last);
    assert(last <= str.size());
    size_t value = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): std::from_chars takes pointer pairs, there is no span/range overload
    auto [ptr, ec] = std::from_chars(str.c_str() + first, str.c_str() + last, value);
    assert(ec == std::errc{});
    return value;
  }

  std::queue<std::vector<std::string>> commands;
  std::vector<std::string> wip_command;
  state _state{state::expect_command};
  size_t args_expected{0};
  size_t arg_len{0};
};

class executor {
public:
  //TODO(amir): singleton
  static std::string execute(const std::vector<std::string>& command) {
    assert(command.size() == 1);
    assert(command[0] == "ping");
    return "+PONG\r\n";
  }

private:
  //TODO(amir): state
};

int main() {
  try {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // NOLINTNEXTLINE(misc-include-cleaner): SIGPIPE is POSIX, canonical home is <signal.h>, which modernize-deprecated-headers bans; <csignal> provides it in practice
  (void)signal(SIGPIPE, SIG_IGN);

  std::map<int, std::pair<connection, parser>> connections;
  epoll_event ev{};
  std::array<epoll_event, MAX_EVENTS> events{};

  const int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg): fcntl is a vararg by signature, there is no non-vararg alternative
  if (fcntl(server_fd, F_SETFL, O_NONBLOCK) != 0) {
    perror("fcntl");
    exit(EXIT_FAILURE);
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  // NOLINTNEXTLINE(misc-include-cleaner): false positive — glibc defines these in bits/socket*.h; <sys/socket.h> is the real provider and is included
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(REDIS_PORT);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): canonical sockaddr idiom of the BSD socket API
  if (bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  const int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  ev.events = EPOLLIN;
  ev.data.fd = server_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) != 0) {
    perror("epoll_ctl: server_fd");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in client_addr{};
  socklen_t client_addr_len = sizeof(client_addr);

  while (true) {
    int nfds = 0;
    do {
      nfds = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
    } while (nfds < 0 && errno == EINTR);
    if (nfds < 0) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    for (int i = 0; i < nfds; ++i) {
      if (events.at(i).data.fd == server_fd) {
        std::cout << "Connecting client...\n";
        int client_fd = -1;
        do {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): canonical sockaddr idiom of the BSD socket API
          client_fd = accept4(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len, SOCK_NONBLOCK);
        } while (client_fd < 0 && errno == EINTR);
        if (client_fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        if (client_fd < 0) {
          continue;
        }
        std::cout << "Client connected\n";

        auto try_emplace_rv = connections.try_emplace(client_fd, std::make_pair(connection{client_fd}, parser{})); // (int, {connection(int), parser()})
        assert(try_emplace_rv.second);
        ev.events = EPOLLIN;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) != 0) {
          perror("epoll_ctl: client_fd");
          exit(EXIT_FAILURE);
        }
      } else {
        std::array<char, RECV_BUF_MAX_SIZE> recv_buf{};

        const int client_fd = events.at(i).data.fd;

        ssize_t bytes_recv = -1;
        do {
          bytes_recv = recv(client_fd, recv_buf.data(), sizeof(recv_buf), 0);
        } while (bytes_recv < 0 && errno == EINTR);
        if (bytes_recv < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          std::cout << "Closing client " << client_fd << " after failed recv\n";
          connections.erase(client_fd);
          continue;
        }
        if (bytes_recv == 0) {
          std::cout << "Client " << client_fd << " closed connection\n";
          connections.erase(client_fd);
          continue;
        }

        auto& [conn, pars] = connections.at(client_fd);
        conn.append(recv_buf, bytes_recv);
        pars.parse_input(conn);
        while (pars.has_command()) {
          const std::string resp = executor::execute(pars.get_command());

          ssize_t bytes_send = -1;
          do {
            bytes_send = send(client_fd, resp.c_str(), resp.size(), 0);
          } while (bytes_send < 0 && errno == EINTR);
          if (bytes_send < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cout << "Closing client " << client_fd << " after failed send\n";
            connections.erase(client_fd);
            break;
          }
        }
      }
    }
  }

  if (close(server_fd) != 0) {
    perror("close: server_fd");
    exit(EXIT_FAILURE);
  }

  exit(EXIT_SUCCESS);
  } catch (const std::exception& e) {
    std::cout << e.what() << '\n';
  }
}
