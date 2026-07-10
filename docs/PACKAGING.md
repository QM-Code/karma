# Packaging

Karma is licensed under the MIT license. See the root `LICENSE` file for the
full license text.

## vcpkg Dependency Manifest

The root `vcpkg.json` is for developers building Karma from this source tree.
It installs Karma's third-party dependencies through vcpkg while CMake still
builds Karma from source.

```bash
vcpkg install --x-manifest-root=.

cmake -S . -B build/vcpkg-headless \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DKARMA_HEADLESS=ON \
  -DKARMA_FETCH_DEPS=OFF \
  -DKARMA_BUILD_EXAMPLES=OFF \
  -DKARMA_BUILD_TESTS=OFF \
  -DBUILD_TESTING=OFF

cmake --build build/vcpkg-headless --parallel
```

The default manifest features match Karma's non-visual default dependencies:
ENet networking, Jolt physics, and Recast/Detour navigation. Use vcpkg feature
selection when building a different backend set.

The `graphical` manifest feature installs GLFW, ImGui, miniaudio, and the local
`diligentcore` overlay port. The DiligentCore overlay port packages the Vulkan
backend and installs a `DiligentCoreConfig.cmake` wrapper that exposes the
Diligent targets Karma expects.

## Local SDK Install

Downstream game repositories should consume Karma from an installed SDK instead
of adding the Karma source tree or enabling `KARMA_CONFIG_FETCH_DEPS`. The SDK
install contains Karma headers, static libraries, built-in runtime shaders,
CMake package files, and the vcpkg dependency package trees needed by the
selected profile.

The default local SDK prefix is:

```bash
/home/quinn/.local/karma
```

One-time prerequisite: install vcpkg and set `VCPKG_ROOT` to that checkout.
The preset uses the repo's `vcpkg-configuration.json`, so the local overlay
ports in `ports/` are active automatically.

Build and install the graphical/navigation debug SDK from this checkout:

```bash
cmake --preset sdk-debug
cmake --build --preset sdk-debug --parallel 1
cmake --install build/sdk-debug
```

The preset also sets `VCPKG_MAX_CONCURRENCY=1` and
`CMAKE_BUILD_PARALLEL_LEVEL=1`, and its build preset uses one job. Keep those
limits in place on memory-constrained machines.

The `sdk-debug` preset builds the server, headless, and graphical profiles with
GLFW, DiligentCore/Vulkan, ImGui, miniaudio, ENet, Jolt, Recast/Detour, Assimp,
KTX, glm, spdlog, and nlohmann-json resolved through vcpkg. It sets
`KARMA_FETCH_DEPS=OFF` and `KARMA_INSTALL_VCPKG_DEPS=ON`; the install step then
copies the active vcpkg triplet into the same SDK prefix before installing
Karma.

Downstream CMake projects should use only the installed prefix:

```bash
cmake -S . -B build/installed-karma \
  -DCMAKE_PREFIX_PATH=/home/quinn/.local/karma
```

```cmake
find_package(karma CONFIG REQUIRED)
target_link_libraries(my_server PRIVATE karma::server)
target_link_libraries(my_app PRIVATE karma::graphical)
```

Do not set `KARMA_CONFIG_FETCH_DEPS=ON` in downstream builds. If
`find_package(karma CONFIG REQUIRED)` cannot resolve a dependency target, rebuild
and reinstall the SDK rather than letting each downstream repository fetch or
rebuild Karma dependencies.

Adding Karma with `add_subdirectory()` or building from this source tree remains
supported for engine development and tests. RPG/game repositories should prefer
the installed SDK path so all forks share the same locally built Karma install.

## vcpkg Overlay Port

The `ports/karma` overlay port packages Karma itself from this checkout.
It supports the server, headless, and graphical profiles:

```bash
vcpkg install karma --classic --overlay-ports=ports
```

To include the graphical profile and its local DiligentCore overlay dependency:

```bash
vcpkg install "karma[graphical,network,physics-jolt,navigation]" --classic --overlay-ports=ports
```

The default port features build:

- `karma::server`
- `karma::headless`
- ENet networking
- Jolt physics
- Recast/Detour navigation

Consumers can use the installed package with the vcpkg toolchain:

```cmake
find_package(karma CONFIG REQUIRED)
target_link_libraries(my_server PRIVATE karma::server)
target_link_libraries(my_tool PRIVATE karma::headless)
target_link_libraries(my_app PRIVATE karma::graphical)
```

Optional overlay features:

- `server`: builds `karma::server`.
- `headless`: builds `karma::headless`.
- `graphical`: builds `karma::graphical` with GLFW, ImGui, miniaudio, and
  DiligentCore/Vulkan.
- `network`: enables ENet.
- `physics-jolt`: enables Jolt.
- `physics-bullet`: enables Bullet. Do not combine with `physics-jolt`.
- `navigation`: enables Recast/Detour.

The local `ports/diligentcore` overlay port is Linux-only. It pins DiligentCore
v2.5.5, clones the required shader-toolchain submodules, uses vcpkg's
`vulkan-headers` and `xxhash` packages where possible, and installs a wrapper
CMake package for Diligent's target names.

## Upstream vcpkg Readiness

The repo now has the pieces needed for package-manager support: an OSS license,
a vcpkg dependency manifest, an overlay port, installable CMake targets, and a
package config smoke-test path.

Before submitting Karma to the builtin vcpkg registry, cut a tagged release and
convert `ports/karma/portfile.cmake` from the local checkout source path to a
`vcpkg_from_github()` release download with a pinned SHA512. Full graphical
registry support also needs the local DiligentCore port to be hardened for the
builtin registry or replaced by upstream-installed DiligentCore CMake package
targets.
