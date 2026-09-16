cmake_minimum_required(VERSION 3.13)

project(redis-starter-cpp)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)

set(CMAKE_CXX_STANDARD 23) # Enable the C++23 standard
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)

add_executable(redis ${SOURCE_FILES})

target_link_libraries(redis PRIVATE Threads::Threads)

# Local-only instrumented builds. CodeCrafters configures without CMAKE_BUILD_TYPE
# (see .codecrafters/compile.sh), so none of these branches run in the remote build.
#   cmake -B build-asan    -DCMAKE_BUILD_TYPE=Asan    && cmake --build build-asan
#   cmake -B build-tsan    -DCMAKE_BUILD_TYPE=Tsan    && cmake --build build-tsan
#     GCC 13's libtsan dies with "unexpected memory mapping" under this kernel's ASLR range.
#     Run it as `setarch $(uname -m) -R ./build-tsan/redis` (or `sudo sysctl vm.mmap_rnd_bits=28`).
#   cmake -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release   # -O3 -DNDEBUG
#   cmake -B build-clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Everything    # clang -Weverything
#
# Every GCC warning that is about this code. Per-flag opt-outs, each with a reason:
#   -Weffc++ - demands an init list entry for every member, including defaulted ones
#   -Wswitch-default - a default label would hide the -Wswitch error on a new enumerator
#   -Wsuggest-attribute=* - the suggestion depends on what the optimizer sees, not on the code
#   -Wsystem-headers - reports libstdc++ internals, not this code
#   -Wlogical-op - EWOULDBLOCK is #defined to EAGAIN on Linux, so `errno != EAGAIN &&
#     errno != EWOULDBLOCK` is a deliberate redundancy kept for systems where they differ;
#     the alternative is #if around every such check
set(STRICT_WARNINGS
    -Werror -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wuseless-cast -Wcast-qual -Wcast-align
    -Wdouble-promotion -Wfloat-equal -Wformat=2 -Wimplicit-fallthrough -Wmisleading-indentation
    -Wnull-dereference -Wnon-virtual-dtor -Woverloaded-virtual -Wsuggest-override -Wctor-dtor-privacy
    -Wextra-semi -Wzero-as-null-pointer-constant -Wundef -Wunused-macros -Wredundant-decls
    -Wduplicated-cond -Wduplicated-branches -Wdisabled-optimization
    -Wstrict-null-sentinel -Wsign-promo -Wswitch-enum -Wdeprecated-copy-dtor -Wmissing-declarations)
if(CMAKE_BUILD_TYPE STREQUAL "Asan")
  target_compile_options(redis PRIVATE ${STRICT_WARNINGS} -O1 -g -fno-omit-frame-pointer
                                       -fsanitize=address,undefined)
  target_compile_definitions(redis PRIVATE _GLIBCXX_DEBUG)
  target_link_options(redis PRIVATE -fsanitize=address,undefined)
elseif(CMAKE_BUILD_TYPE STREQUAL "Tsan")
  target_compile_options(redis PRIVATE ${STRICT_WARNINGS} -O1 -g -fno-omit-frame-pointer
                                       -fsanitize=thread)
  target_link_options(redis PRIVATE -fsanitize=thread)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(redis PRIVATE ${STRICT_WARNINGS})
elseif(CMAKE_BUILD_TYPE STREQUAL "Everything")
  # clang++ only: every warning clang has, including the ones LLVM itself calls excessive.
  # Opt-outs, each with a reason:
  #   c++98-compat / pre-c++2x-compat - this is a C++23 codebase on purpose
  #   -Wpadded - the layout of the kernel's structs is not ours to fix
  #   -Wunsafe-buffer-usage - wants std::span in place of every POSIX buffer + length API
  #   -Wswitch-default - same reason as for GCC above
  target_compile_options(redis PRIVATE -Werror -Weverything -O1 -g
                                       -Wno-c++98-compat -Wno-c++98-compat-pedantic
                                       -Wno-pre-c++14-compat -Wno-pre-c++17-compat
                                       -Wno-pre-c++20-compat-pedantic -Wno-pre-c++23-compat
                                       -Wno-padded -Wno-unsafe-buffer-usage -Wno-switch-default)
endif()