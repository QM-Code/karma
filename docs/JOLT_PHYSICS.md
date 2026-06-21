# Jolt Physics Integration

This page documents the Jolt-facing physics work added to Karma's simulation
layer. The API is intentionally exposed through backend-neutral Karma types and
ECS components, with Jolt as the backend that currently implements the full
surface described here.

## Build

The default graphical profile enables Jolt:

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON
cmake --build build --parallel
```

The relevant CMake option is:

```bash
-DKARMA_PHYSICS_BACKEND_JOLT=ON
```

Only one physics backend should be enabled at a time. Jolt is the production
backend. Bullet remains available only as an experimental backend and may lag
the ECS physics feature set.

## Public Surface

The public simulation API lives under `include/karma/physics.h