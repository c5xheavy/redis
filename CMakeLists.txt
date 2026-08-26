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
set(STRICT_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Werror)
if(CMAKE_BUILD_TYPE STREQUAL "Asan")
  target_compile_options(redis PRIVATE ${STRICT_WARNINGS} -O1 -g -fno-omit-frame-pointer
                                       -fsanitize=address,undefined)
  target_compile_definitions(redis PRIVATE _GLIBCXX_DEBUG)
  target_link_options(redis PRIVATE -fsanitize=address,undefined)
elseif(CMAKE_BUILD_TYPE STREQUAL "Tsan")
  target_compile_options(redis PRIVATE ${STRICT_WARNINGS} -O1 -g -fno-omit-frame-pointer
                                       -fsanitize=thread)
  target_link_options(redis PRIVATE -fsanitize=thread)
endif()