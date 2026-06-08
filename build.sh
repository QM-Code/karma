#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

build_graphical=1
build_headless=0
headless_profile="headless"
build_examples="ON"
build_tests="ON"
build_rmlui="OFF"
clean=0
config="Release"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-}"

if [[ -z "${jobs}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.ncpu)"
  else
    jobs="2"
  fi
fi

usage() {
  cat <<'USAGE'
Usage: ./build.sh [options]

Default: configure and build the normal graphical profile, including examples
and tests. Headless is not built unless requested.

Options:
  --headless              Also build the regular headless profile.
  --headless-only         Build only the regular headless profile.
  --minimal-headless      Build the smaller headless profile.
  --no-examples           Do not generate or build example executables.
  --no-tests              Do not generate or build test executables.
  --rmlui                 Build the RmlUi adapter/demo in graphical builds.
  --clean                 Remove selected build directories before configure.
  --config <name>         Build configuration for multi-config generators.
                          Default: Release.
  --jobs <count>          Parallel build job count.
  -h, --help              Show this help.

Examples:
  ./build.sh
  ./build.sh --headless
  ./build.sh --headless-only --minimal-headless --no-examples --no-tests
  ./build.sh --no-examples --config Debug --jobs 4
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --headless)
      build_headless=1
      ;;
    --headless-only)
      build_graphical=0
      build_headless=1
      ;;
    --minimal-headless)
      build_headless=1
      headless_profile="minimal-headless"
      ;;
    --no-examples)
      build_examples="OFF"
      ;;
    --no-tests)
      build_tests="OFF"
      ;;
    --rmlui)
      build_rmlui="ON"
      ;;
    --clean)
      clean=1
      ;;
    --config)
      shift
      if [[ $# -eq 0 ]]; then
        echo "error: --config requires a value" >&2
        exit 2
      fi
      config="$1"
      ;;
    --jobs|-j)
      shift
      if [[ $# -eq 0 ]]; then
        echo "error: --jobs requires a value" >&2
        exit 2
      fi
      jobs="$1"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option '$1'" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

run_profile() {
  local profile="$1"
  local build_dir="${ROOT_DIR}/build/${profile}"
  local cmake_args=(
    -S "${ROOT_DIR}"
    -B "${build_dir}"
    -DKARMA_FETCH_DEPS=ON
    -DKARMA_BUILD_EXAMPLES="${build_examples}"
    -DKARMA_BUILD_TESTS="${build_tests}"
    -DBUILD_TESTING="${build_tests}"
    -DCMAKE_BUILD_TYPE="${config}"
  )

  case "${profile}" in
    portable)
      cmake_args+=(
        -DKARMA_HEADLESS=OFF
        -DKARMA_BUILD_RMLUI_DEMO="${build_rmlui}"
      )
      ;;
    headless)
      cmake_args+=(
        -DKARMA_HEADLESS=ON
      )
      ;;
    minimal-headless)
      cmake_args+=(
        -DKARMA_HEADLESS=ON
        -DKARMA_ENABLE_AUDIO=OFF
        -DKARMA_ENABLE_NAVIGATION=OFF
        -DKARMA_NETWORK_BACKEND_ENET=OFF
        -DKARMA_PHYSICS_BACKEND_JOLT=OFF
        -DKARMA_PHYSICS_BACKEND_BULLET=OFF
      )
      ;;
    *)
      echo "error: unknown build profile '${profile}'" >&2
      exit 2
      ;;
  esac

  if [[ "${clean}" -eq 1 ]]; then
    cmake -E rm -rf "${build_dir}"
  fi

  echo "==> Configuring ${profile}"
  cmake "${cmake_args[@]}"

  echo "==> Building ${profile}"
  cmake --build "${build_dir}" --config "${config}" --parallel "${jobs}"
}

if [[ "${build_graphical}" -eq 1 ]]; then
  run_profile "portable"
fi

if [[ "${build_headless}" -eq 1 ]]; then
  run_profile "${headless_profile}"
fi
