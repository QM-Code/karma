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
