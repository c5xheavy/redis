#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "server.hpp"

int main() {
  try {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // NOLINTNEXTLINE(misc-include-cleaner): SIGPIPE is POSIX, canonical home is <signal.h>, which modernize-deprecated-headers bans; <csignal> provides it in practice
    (void)signal(SIGPIPE, SIG_IGN);

    redis::server server;
    server.serve();

    exit(EXIT_SUCCESS);
  } catch (const std::exception& e) {
    std::cout << e.what() << '\n';
  }
}
