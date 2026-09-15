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

  step "[1/5] build: build, build-asan, build-tsan, build-release"
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

  step "[2/5] clang-tidy"
  # Any finding stops the script before the server runs (zero-findings baseline).
  # --header-filter: default reports the main file only; our headers are user code.
  clang-tidy -p build --quiet --warnings-as-errors='*' --header-filter='src/.*' src/*.cpp
  # Second pass: each header compiled standalone as its own TU. Main-file-only
  # checks (misc-include-cleaner) police headers only this way — IWYU per file.
  clang-tidy --quiet --warnings-as-errors='*' src/*.hpp -- -x c++ -std=c++23
  echo "clang-tidy: clean"

  step "[3/5] regression suite (Asan build)"
  # Any FAIL stops the script before the server runs. SKIP_SUITE=1 to bypass
  # while debugging a known-red scenario.
  if [ -n "${SKIP_SUITE}" ]; then
    echo "suite: skipped (SKIP_SUITE set)"
  else
    python3 suite.py
  fi

  step "[4/5] clang-format"
  # Check only, never rewrites; apply with: clang-format -i src/*.cpp src/*.hpp
  clang-format --dry-run --Werror src/*.cpp src/*.hpp
  echo "clang-format: clean"
)

step "[5/5] run: build/redis $*"
# Copied from .codecrafters/run.sh
#
# - Edit this to change how your program runs locally
# - Edit .codecrafters/run.sh to change how your program runs remotely
exec $(dirname "$0")/build/redis "$@"
