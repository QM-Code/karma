# Public Header Rules

New public headers must live under the layered layout and be included using the
same public path, for example:

- `karma/world/components/transform.h`
- `karma/simulation/physics/physics_system.h`
- `karma/features/visual/particles/particle_system.h`

Do not add forwarding headers for old include paths.

Public headers should expose data contracts and narrow subsystem APIs. Keep
backend internals and large implementation records in `src/` unless users
actually need them.
