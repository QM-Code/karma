# Consumer Profiles And Versioning

Karma now exposes engine profiles as the public CMake contract. Application
code should choose a Karma profile target and include Karma engine headers; it
should not name renderer, physics, navigation, network, audio, window, or UI
backend implementation targets directly.

## Public Targets

- `karma::headless`: server/non-visual runtime profile.
- `karma::server`: minimal ECS/network server profile.
- `karma::graphical`: full graphical runtime profile.
- `karma::karma`: compatibility alias for `karma::graphical` when the
  graphical profile is built.

`karma::server` is intended for lightweight dedicated servers and networking
tests. It links core ECS, platform networking, and the networking feature layer
only; games opt into content, physics, navigation, or runtime libraries
explicitly.

`karma::headless` is intended for servers, simulation/gameplay tests, and
tools that do not need a window or GPU. It includes core ECS/runtime APIs,
content import, animation, collision, physics, navigation, and networking when
those subsystems are enabled. It does not build graphical window, Diligent
renderer, graphical UI provider, debug UI, or audio backends.

`karma::graphical` is the complete runtime profile used by examples. It includes
the headless-capable subsystems plus graphical window, renderer, visual feature,
debug UI, ImGui UI, and audio backend support.

## Profile Headers

- Include `<karma/server.h>` for the minimal ECS/network server umbrella.
- Include `<karma/headless.h>` for the server/non-visual umbrella.
- Include `<karma/karma.h>` for the full graphical umbrella.
- Prefer narrower `karma/<layer>/...` headers in reusable libraries.

The headless umbrella avoids graphical UI provider and backend headers. Some
renderer-facing public types remain available because world components, content
import, and runtime abstractions use them, but they do not require the Diligent
backend in a headless build.

## Build Options

`KARMA_HEADLESS=ON` remains the shortcut for a non-visual build. It forces
`KARMA_BUILD_HEADLESS_PROFILE=ON` and `KARMA_BUILD_GRAPHICAL_PROFILE=OFF`.
`KARMA_BUILD_SERVER_PROFILE` defaults to `ON` and remains independently
controllable.

For direct profile control:

- `KARMA_BUILD_SERVER_PROFILE=ON|OFF`
- `KARMA_BUILD_HEADLESS_PROFILE=ON|OFF`
- `KARMA_BUILD_GRAPHICAL_PROFILE=ON|OFF`

When the graphical profile is off, CMake also disables graphical window,
renderer, graphical UI provider, debug UI, and audio backend options.

Headless is not the same as minimal. Content import, physics, navigation, and
networking remain controlled by their own options. Use the `minimal-headless`
preset or explicitly disable unused subsystem backends for the smallest
non-visual build.

## Installed Packages

Installed packages default `KARMA_CONFIG_FETCH_DEPS` to `OFF`. A consumer using
`find_package(karma CONFIG REQUIRED)` is expected to make required dependency
targets available through the install prefix or the normal CMake package search
path.

Set `KARMA_CONFIG_FETCH_DEPS=ON` before `find_package(karma)` only when a
consumer intentionally wants Karma's package config to fetch missing third-party
dependencies at package-import time.

## Versioning

The root `VERSION` file is the source of truth for:

- `project(karma VERSION ...)`
- generated package version files
- generated public header `<karma/version.h>`

Karma uses SemVer `0.x` while the engine API is still moving quickly. Breaking
changes are allowed before `1.0.0`, but the version should still be updated
intentionally when the public consumer contract changes.

## Validation

The profile split was validated with:

```bash
cmake --preset minimal-headless
cmake --build --preset minimal-headless --parallel 4
ctest --test-dir build/minimal-headless --output-on-failure
```

Additional smoke checks covered:

- source-vendored consumer linking `karma::server`
- source-vendored consumer linking `karma::headless`
- installed-package consumer linking `karma::server` and `karma::headless`
- source-vendored graphical consumer linking `karma::graphical`
- source-vendored alias consumer linking `karma::karma`
- full default graphical profile build with GLFW, Diligent/Vulkan, miniaudio,
  ENet, Jolt, Recast/Detour, debug UI, and ImGui
