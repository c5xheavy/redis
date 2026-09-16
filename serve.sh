#!/bin/sh
#
# Use this script to run your program LOCALLY.
#
# Note: Changing this script WILL NOT affect how CodeCrafters runs your program.
#
# Learn more: https://codecrafters.io/program-interface

set -e # Exit early if any commands fail

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

# Copied from .codecrafters/compile.sh
#
# - Edit this to change how your program compiles locally
# - Edit .codecrafters/compile.sh to change how your program compiles remotely
(
  cd "$(dirname "$0")" # Ensure compile steps are run within the repository directory

  step "[1/6] build: build, build-asan, build-tsan, build-release"
  # No vcpkg dependencies are used; only pass the toolchain if vcpkg is actually installed
  if [ -n "${VCPKG_ROOT}" ]; then
    cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake >/dev/null
  else
    cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
  fi
  cmake --build ./build

  # Local-only instrumented builds (see CMakeLists.txt). All of them must compile —
  # -Werror lives there — before the plain build below is run.
  cmake -B build-asan    -S . -DCMAKE_BUILD_TYPE=Asan    >/dev/null && cmake --build build-asan
  cmake -B build-tsan    -S . -DCMAKE_BUILD_TYPE=Tsan    >/dev/null && cmake --build build-tsan
  cmake -B build-release -S . -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build-release

  step "[2/6] clang-tidy"
  # Any finding stops the script before the server runs (zero-findings baseline).
  # --header-filter: default reports the main file only; our headers are user code.
  clang-tidy -p build --quiet --warnings-as-errors='*' --header-filter='src/.*' src/*.cpp
  echo "clang-tidy: clean"

  step "[3/6] include-what-you-use"
  # The real IWYU, not clang-tidy's approximation of it: every TU, and every project
  # header through --check_also (IWYU reports only the main file and its associated
  # header otherwise). --error=1 makes any suggestion a non-zero exit, so set -e stops here.
  iwyu_tool -p build src/*.cpp -- -Xiwyu --error=1 -Xiwyu --check_also='src/*.hpp' \
    -Xiwyu --max_line_length=120
  echo "include-what-you-use: clean"

  step "[4/6] regression suite (Asan build)"
  # Any FAIL stops the script before the server runs. SKIP_SUITE=1 to bypass
  # while debugging a known-red scenario.
  if [ -n "${SKIP_SUITE}" ]; then
    echo "suite: skipped (SKIP_SUITE set)"
  else
    python3 suite.py
  fi

  step "[5/6] clang-format"
  # Check only, never rewrites; apply with: clang-format -i src/*.cpp src/*.hpp
  clang-format --dry-run --Werror src/*.cpp src/*.hpp
  echo "clang-format: clean"
)

step "[6/6] run: build/redis $*"
# Copied from .codecrafters/run.sh
#
# - Edit this to change how your program runs locally
# - Edit .codecrafters/run.sh to change how your program runs remotely
exec $(dirname "$0")/build/redis "$@"
