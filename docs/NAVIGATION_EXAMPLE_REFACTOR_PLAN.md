# Navigation Example Refactor Plan

## Problem

`examples/navmesh_example.cpp` had grown into a mixed engine sample and utility
module. It owned and updated navigation directly while also handling scene
authoring helpers, debug marker mesh generation, camera picking math, lighting
setup, navigation diagnostics, debug drawing, and demo gameplay.

That made the example harder to read and also hid reusable behavior from the
engine. A click-to-move sample should mostly describe the scene, convert player
intent into `NavigationSystem::requestMoveTo`, and update demo-specific
presentation such as the target marker and follow camera.

## Refactor Shape

- Runtime scene helpers provide common entity setup:
  - mesh entities from asset keys
  - simple owned mesh/material entities
  - box debug markers
  - camera, light, and environment entities
- Renderer camera picking exposes a reusable screen-point-to-world-ray helper.
- Navigation is registered in the engine `SystemGraph` so examples no longer
  own or tick `NavigationSystem`.
- Navigation diagnostics live next to navigation and preserve
  `KARMA_NAVMESH_DIAG=1`.
- `navmesh_example.cpp` keeps scene-specific setup and gameplay intent:
  - load world and tank assets
  - mark the world as a navmesh surface
  - create a navmesh owner and tank agent
  - on click, request movement and move the target marker
  - follow the tank with the camera

## Boundaries

This cleanup intentionally does not add tiled navmeshes, obstacle carving,
DetourCrowd, serialized navmesh assets, or a general picking/collision API.
Floor hit testing remains in the example because the engine does not yet have a
broader gameplay picking abstraction.

## Acceptance Checks

- `karma_navmesh_example` builds and renders the same world/tank setup.
- Left-clicking the floor requests movement through `NavigationSystem`.
- Existing replacement-path behavior is preserved by the navigation system.
- The target marker appears at the latest destination.
- Navigation debug rendering is still available through
  `NavigationSystem::debugDraw`.
- `KARMA_NAVMESH_DIAG=1` still logs request diagnostics.
