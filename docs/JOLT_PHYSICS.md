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

The public simulation API lives under `include/karma/simulation/physics/`.
Gameplay code can use the low-level facade directly through
`karma::physics::World`, while normal ECS gameplay should prefer the components
under `include/karma/world/components/`.

Key low-level entry points:

- `physics::World::createBody`: creates a rigid body from `PhysicsBodyDesc`.
- `physics::World::createCharacterController`: creates a kinematic character
  controller from a box extent.
- `physics::World::createConstraint`: creates a two-body constraint from
  `PhysicsConstraintDesc` and two native rigid-body handles.
- `physics::World::createVehicle`: creates a vehicle constraint/controller from
  `PhysicsVehicleDesc` and a chassis body handle.
- `physics::World::createSoftBody`: creates a soft body from
  `PhysicsSoftBodyDesc`.
- `physics::World::castRay`, `castRayAll`, `collidePoint`, `collideShape`, and
  `castShape`: expose filtered Jolt query paths.

The move-only runtime wrappers are:

- `physics::RigidBody`
- `physics::CharacterController`
- `physics::Constraint`
- `physics::Vehicle`
- `physics::SoftBody`

Each wrapper owns a backend object, exposes `isValid()`/`nativeHandle()` for
diagnostics, and destroys the backend object when the wrapper is destroyed.

## ECS Components

The physics system consumes ECS components during fixed-step updates and keeps
backend objects synchronized with authored state.

Public physics components are authoring and command/status contracts. Backend
objects, native handles, contact caches, vehicle telemetry buffers, soft-body
vertex snapshots, and other heavy runtime payloads stay owned by the physics
system or by direct low-level wrappers.

The ECS physics update is organized as a fixed-step pipeline:

1. Resolve authored resources such as mesh collider geometry.
2. Synchronize backend objects from component composition and authored config.
3. Apply per-tick commands such as forces, impulses, controller velocity, and
   vehicle driver input.
4. Step the backend world.
5. Publish transforms and small public status fields back to components.
6. Publish contact/ground event buffers and remove backend objects for dead or
   invalid entities.

### Rigid Bodies

Rigid-body authoring continues to use the existing Karma components:

- `components::RigidbodyComponent`
- `components::ColliderComponent` with box, sphere, capsule, cylinder, mesh,
  height field, convex hull, triangle, or tapered capsule shape data
- `components::PhysicsMaterialComponent`
- `components::PhysicsCollisionFilterComponent`
- `components::PhysicsBodyForcesComponent`
- contact and grounded-state listener components

The Jolt backend now maps the richer rigid-body description:

- static, kinematic, and dynamic motion
- discrete and linear-cast motion quality
- allowed degree-of-freedom masks
- mass, inertia multiplier, damping, max velocities, solver-step overrides
- friction and restitution
- sensors/triggers
- collision layer and mask filtering
- sleeping, dynamic/kinematic switching, kinematic-vs-non-dynamic collisions
- manifold reduction, gyroscopic force, enhanced internal edge removal
- runtime shape replacement
- force, torque, impulse, point-force, point-impulse, angular impulse, velocity,
  gravity-factor, activation, sleep timer, user-data, and buoyancy controls

### Character Controllers

`components::CharacterControllerComponent` authors a kinematic character
controller on an ECS entity. The entity must already have
`components::TransformComponent` and a box `components::ColliderComponent`.
Other collider shapes are rejected by component validation.

The physics system owns one backend character controller per ECS component.
There is no global default controller. Gameplay drives the controller through
component commands:

- `setDesiredVelocity`
- `setDesiredAngularVelocity`
- `addImpulse`
- `setAddVelocity`
- `clearImpulse`

After the fixed physics step, the system writes small status fields back to the
component:

- `velocity`
- `angular_velocity`
- `forward`
- `grounded`

Set `TransformComponent` local/world position or rotation to reset or teleport
the controller. Dirty transform state is pushed into the backend on the next
fixed update before controller commands are applied.

Prefab `CharacterControllerComponent` payloads are strict and use:

- `enabled`
- `desired_velocity`
- `desired_angular_velocity`
- `add_velocity`

### Constraints

`components::PhysicsConstraintComponent` authors a two-body Jolt-style
constraint on a constraint entity. The component references `body_a` and
`body_b`, and the physics system creates/rebuilds the backend constraint when
the authored configuration changes.

Supported constraint kinds:

- `Fixed`
- `Point`
- `Distance`
- `Hinge`
- `Slider`
- `Cone`
- `SwingTwist`
- `SixDof`

Supported settings include world/local frames, priority, enabled state, solver
overrides, draw size, user data, auto-detected points, axes/normals, distance
limits, angular limits, cone and twist limits, linear/angular friction, springs,
and six-DOF per-axis limits/friction.

### Vehicles

`components::PhysicsVehicleComponent` authors a Jolt vehicle attached to an ECS
rigid body. The component stores creation settings and per-frame driver input.
Vehicle backend objects and telemetry are owned by the physics system.

Supported controllers:

- `Wheeled`
- `Motorcycle`
- `Tracked`

Supported vehicle settings include:

- ray, sphere-cast, and cylinder-cast wheel collision testers
- up/forward axes, pitch/roll limit, gravity override, solver overrides,
  priority, collision-test cadence, and user data
- wheel hardpoints, suspension force points, suspension direction, steering
  axis, wheel up/forward axes, suspension lengths, preload, spring settings,
  radius, width, suspension-force-point toggle
- wheeled/motorcycle wheel inertia, damping, steer angle, friction curves,
  brake torque, and handbrake torque
- engine torque/RPM/inertia/damping curves
- automatic and manual transmission data
- differentials, limited slip, and anti-roll bars
- motorcycle lean-controller settings
- tracked vehicle tracks, driven wheels, track inertia/damping/brake torque,
  differential ratio, and left/right input ratios

Runtime telemetry includes the native vehicle handle, active flag, engine RPM,
gear, clutch state, wheel speed at clutch, tracked angular velocities, and per
wheel contact/suspension/steering/spin data. It is not stored on the ECS
component. Direct low-level users can read this state through
`physics::Vehicle::getState()`.

### Soft Bodies

`components::PhysicsSoftBodyComponent` authors soft bodies through ECS. The
component supports procedural presets and custom topology.

Supported presets:

- `Custom`
- `Cloth`
- `Cube`
- `Sphere`

Supported authoring data:

- vertices with velocity and inverse mass
- triangle faces
- edge constraints
- volume constraints
- pinned vertices
- grid and sphere generation settings
- generated constraint toggles, optimization, bend type, vertex attributes,
  angle tolerance, and vertex radius
- material, collision layers/masks, solver iterations, damping, max velocity,
  pressure, gravity factor, update-position mode, rotation handling, sleeping,
  and activation

Runtime state includes the native soft-body handle, active flag, transform,
volume, solver iterations, pressure, update-position flag, vertex positions,
vertex velocities, inverse masses, and triangle indices. It is not stored on
the ECS component. Direct low-level users can read this state through
`physics::SoftBody::getState()`.

The low-level `physics::SoftBody` wrapper also exposes pressure updates,
position-update toggles, skin-constraint toggles, skinned max-distance
multiplier, per-vertex positioning, activation, deactivation, and state reads.

## Query API

The Jolt query API is exposed through backend-neutral descriptors:

- `PhysicsRaycastDesc`
- `PhysicsShapeQueryDesc`
- `PhysicsShapeCastDesc`
- `PhysicsQueryFilter`
- `PhysicsQueryHit`

Current query coverage:

- closest raycast
- all-hit raycast, sorted nearest first
- point overlap
- shape overlap
- linear shape cast
- collision masks
- sensor inclusion/exclusion
- single ignored body handle
- multiple ignored body handles
- back-face modes
- convex-as-solid mode
- shrunken shape casts
- deepest-point shape-cast mode

## Examples

Backend-neutral physics examples live under `examples/physics/`. They exercise
the public physics API; this backend currently provides the full implementation
for those features. Their CMake targets use the `physics_` category prefix, and
their executables are emitted under `build/examples/physics/`.

Build one example:

```bash
cmake --build build --target physics_car --parallel
./build/examples/physics/car
```

Build all physics examples:

```bash
cmake --build build --parallel --target \
  physics_shape_gallery \
  physics_body_controls \
  physics_query_lab \
  physics_constraint_lab \
  physics_car
```

Available examples:

- `physics_shape_gallery`: ECS rigid-body shape gallery covering dynamic primitives,
  static triangle/mesh/height-field surfaces, material changes, CCD, sleeping,
  gyroscopic force, manifold reduction, triggers, allowed DOFs, collision
  filters, impulses, continuous forces, contact listeners, and grounded state.
- `physics_body_controls`: direct body-control lab covering shape replacement,
  kinematic toggling, motion quality, gravity, triggers, activation/sleep,
  linear/angular velocities, forces, torques, impulses, point velocities,
  teleports, material/user-data reads, and buoyancy.
- `physics_query_lab`: ray, all-ray, point-overlap, shape-overlap, and
  shape-cast lab with filter masks, sensor inclusion, ignored handles,
  back-face modes, scale, max-separation, shrunken casts, and deepest-point
  casts.
- `physics_constraint_lab`: ECS constraint editor for fixed, point, distance, hinge,
  slider, cone, swing-twist, and six-DOF constraints with limits, friction,
  springs, solver overrides, priority, local/world frames, and live impulses.
- `physics_car`: graphical wheeled vehicle example with an ImGui tuning panel, wheel
  telemetry, contact visualization, suspension visualization, road obstacles,
  chase/free camera modes, drive mode selection, collision tester selection,
  engine/transmission tuning, suspension tuning, tire friction, braking,
  anti-roll, differential ratio, reset, and upright controls.

Common fly-camera examples use `WASD`, `Q/E`, and right mouse. The car example
uses arrow keys or `WASD` for driving, Shift for brake, Space for handbrake,
`R` to reset, and `F` to upright the chassis.

## Tests

`karma_physics_tests` now covers the Jolt-facing surface at a smoke-test level:

- contact filtering
- ray/shape query behavior
- rigid-body runtime controls
- character controller stability, grounding, and multi-controller ownership
- vehicle creation/input and low-level wrapper state reporting
- soft-body creation and low-level wrapper state reporting

Run the test target through CTest:

```bash
ctest --test-dir build --output-on-failure
```

## Current Backend Notes

The public API is backend-neutral, but feature completeness depends on the
selected backend:

- Jolt is the implementation target for character controllers, constraints,
  queries, vehicles, and soft bodies.
- Bullet is experimental and may return null or incomplete backend objects for
  newer ECS physics features.
- Mesh collision should be authored through ECS mesh collider geometry or
  `PhysicsShapeDesc` mesh data. The old static mesh path importer is no longer
  the preferred route.
