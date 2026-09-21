#!/usr/bin/env bash

select_llvm21_toolchain() {
  local compiler_dir compiler_version compatible_sdk macos_major sdk_major sdk_root

  if test -n "${GRAPHX_LLVM_CC:-}" || test -n "${GRAPHX_LLVM_CXX:-}"; then
    test -n "${GRAPHX_LLVM_CC:-}" && test -n "${GRAPHX_LLVM_CXX:-}" || {
      echo "set both GRAPHX_LLVM_CC and GRAPHX_LLVM_CXX" >&2
      return 2
    }
    CC=$GRAPHX_LLVM_CC
    CXX=$GRAPHX_LLVM_CXX
  elif test "$(uname -s)" = Darwin; then
    command -v brew >/dev/null || {
      echo "LLVM 21 testing on macOS requires Homebrew llvm@21" >&2
      return 2
    }
    compiler_dir=$(brew --prefix llvm@21)/bin
    CC=$compiler_dir/clang
    CXX=$compiler_dir/clang++
    CLANG_FORMAT=${CLANG_FORMAT:-$compiler_dir/clang-format}
    CLANG_TIDY=${CLANG_TIDY:-$compiler_dir/clang-tidy}
    export CLANG_FORMAT CLANG_TIDY
    command -v xcrun >/dev/null || {
      echo "LLVM 21 testing on macOS requires Xcode Command Line Tools" >&2
      return 2
    }
    macos_major=$(sw_vers -productVersion | cut -d. -f1)
    sdk_root=${SDKROOT:-$(xcrun --show-sdk-path)}
    sdk_major=$(xcrun --show-sdk-version | cut -d. -f1)
    if test -z "${SDKROOT:-}" && test "$sdk_major" -gt "$macos_major"; then
      compatible_sdk=$(dirname "$sdk_root")/MacOSX${macos_major}.sdk
      if test -d "$compatible_sdk"; then
        echo "NOTE: LLVM 21 selects installed macOS $macos_major SDK instead of newer SDK $sdk_major."
        sdk_root=$compatible_sdk
      fi
    fi
    test -d "$sdk_root" || {
      echo "active macOS SDK not found: $sdk_root" >&2
      return 2
    }
    if ! printf '#include <random>\n' | "$CXX" -x c++ -std=c++20 -fsyntax-only \
        -isysroot "$sdk_root" - >/dev/null 2>&1; then
      compatible_sdk=$(dirname "$sdk_root")/MacOSX26.sdk
      if test -n "${SDKROOT:-}" || ! test -d "$compatible_sdk" || \
          ! printf '#include <random>\n' | "$CXX" -x c++ -std=c++20 -fsyntax-only \
            -isysroot "$compatible_sdk" - >/dev/null 2>&1; then
        echo "LLVM 21 cannot compile standard <random> with SDK $sdk_root; select a compatible SDKROOT." >&2
        return 2
      fi
      echo "NOTE: active SDK headers are incompatible with LLVM 21; using verified $compatible_sdk."
      sdk_root=$compatible_sdk
    fi
    SDKROOT=$sdk_root
    export SDKROOT
    if test "$macos_major" -ge 26 && test -z "${GRAPHX_SANITIZERS:-}"; then
      GRAPHX_SANITIZERS=undefined
      export GRAPHX_SANITIZERS
      echo "NOTE: macOS $macos_major uses LLVM 21 UBSan without ASan because the"
      echo "Homebrew LLVM ASan runtime hangs during process initialization on macOS 26."
      echo "Full LLVM 21 ASan+UBSan acceptance remains enabled on Linux."
    fi
  else
    CC=clang-21
    CXX=clang++-21
  fi

  command -v "$CC" >/dev/null || { echo "LLVM 21 compiler not found: $CC" >&2; return 2; }
  command -v "$CXX" >/dev/null || { echo "LLVM 21 compiler not found: $CXX" >&2; return 2; }
  compiler_version=$("$CXX" --version)
  if grep -q "Apple clang" <<<"$compiler_version" || ! grep -q "version 21\." <<<"$compiler_version"; then
    echo "GraphX verification requires LLVM 21.x; found: $compiler_version" >&2
    return 2
  fi

  compiler_dir=$(dirname "$(command -v "$CXX")")
  if test -x "$compiler_dir/llvm-symbolizer"; then
    ASAN_SYMBOLIZER_PATH=$compiler_dir/llvm-symbolizer
    export ASAN_SYMBOLIZER_PATH
  elif command -v llvm-symbolizer-21 >/dev/null; then
    ASAN_SYMBOLIZER_PATH=$(command -v llvm-symbolizer-21)
    export ASAN_SYMBOLIZER_PATH
  fi
  export CC CXX
  echo "LLVM compiler: CC=$CC CXX=$CXX"
  test -z "${SDKROOT:-}" || echo "macOS SDK: SDKROOT=$SDKROOT"
}