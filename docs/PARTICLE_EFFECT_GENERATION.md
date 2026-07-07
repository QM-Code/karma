# Particle Effect Generation

Karma has a constrained particle-generation path for agents and tools. Agents
write small `*.kpspec.json` files, then the content tools expand those specs into
a prefab package containing validated `.kpeffect` files, copied texture assets,
and a `prefab.json` composition.

Generated packages keep the same authoring boundaries as hand-written content:

- `.kpeffect`: v3 JSON emitter layers for sparks, smoke, halos, heat, impacts,
  and other particle simulation.
- `ParticleBeamComponent`: prefab/component data for continuous textured
  beams/ribbons such as fire rays and magic-missile trails.
- `prefab.json`: transform hierarchy and component composition.
- `assets.package.json`: texture imports, particle effect registrations, and the
  prefab entry.

## Build Targets

Enable tool targets with `KARMA_BUILD_TOOLS`:

```bash
cmake -S . -B build -DKARMA_BUILD_TOOLS=ON
cmake --build build --target \
  karma_particle_effect_validate \
  karma_particle_effect_format \
  karma_particle_effect_generate
```

Top-level builds enable tools by default. Consumer builds can opt in with the
CMake option above.

## Specs

The v1 generation schema lives at
[`../schemas/particles/kpspec.v1.schema.json`](../schemas/particles/kpspec.v1.schema.json).
The current generator accepts these presets:

- `fire_ray`: a continuous additive beam plus sparks, smoke, and heat haze.
- `magic_missile`: a continuous additive trail plus head halo and trailing
  sparks.
- `arcane_barrage`: six teal-white missile ribbons with caster flare, comet
  heads, sparks, mist, and distortion.
- `blade_barrier`: a shrouded spherical barrier of spinning blade shards with
  moving wind rings, swish streaks, dust, glints, and subtle distortion.
- `breathe_fire`: a wide, source-point flame cone with dense additive ribbons,
  embers, smoke, and heat haze.
- `fireball`: an instantaneous spherical burst with a hot flash, OpenAI-sourced
  core/smoke flipbooks, turbulent flame lobes, embers, smoke edge, and heat
  shimmer.
- `chromatic_ray`: a prismatic ray with twisting helix ribbons, sparks, wisps,
  and subtle distortion.
- `daze`: a blue-purple burst halo with stars, crescents, haze, pulse, and no
  orb shell.
- `heal`: a blue healing shimmer with stacked rings, glints, mist, pulse, and
  subtle distortion.
- `haste`: a golden-cyan speed aura with circular body rings, vertical streaks,
  afterimage haze, tick sparks, distortion, light, and prefab variables for
  creature-scale overrides.
- `detect_magic`: a 30-foot white emanation with a volumetric shimmer field,
  pixie dust, thin mist, and light distortion.
- `impact_burst`: a particle-only burst with flash, sparks, smoke, and shock
  ring.
- `energy_orb`: a mesh shell plus local-space core, arc, halo, distortion, and
  light layers.

Minimal spec:

```json
{
  "version": 1,
  "preset": "fire_ray",
  "namespace": "generated/fire_ray",
  "name": "Fire Ray",
  "length": 5.0
}
```

For bent beams, provide local path points instead of relying on `length`:

```json
{
  "version": 1,
  "preset": "magic_missile",
  "namespace": "generated/magic",
  "path_points": [
    [0.0, 0.0, 0.0],
    [1.5, 0.2, 0.0],
    [3.0, 0.0, 0.4]
  ]
}
```

For orb effects, `radius` controls the generated shell, particles, and light:

```json
{
  "version": 1,
  "preset": "energy_orb",
  "namespace": "generated/energy_orb",
  "name": "Energy Orb",
  "radius": 1.15
}
```

## Generate

Run the generator with the spec path and output directory:

```bash
./build/karma_particle_effect_generate effects/fire_ray.kpspec.json generated/fire_ray
```

The output directory is a package root:

```text
generated/fire_ray/
  assets.package.json
  prefab.json
  particles/
    fire_ray_sparks.kpeffect
    fire_ray_smoke.kpeffect
    fire_ray_heat.kpeffect
  textures/
    ...
```

The generator copies curated in-repo atlas assets into the package and registers
them under namespaced texture keys. It also validates every generated
`.kpeffect` before returning.

Checked-in generated spell examples live under `examples/assets/prefabs/` with
stable `prefabs/<effect>/...` asset keys. The source specs remain under
`examples/particles/specs/`, and `particles_generated_preview` can load either
the checked-in package directory or a spec file.

## Validate And Format

Validate any hand-authored or generated `.kpeffect` files:

```bash
./build/karma_particle_effect_validate examples/assets/prefabs/explosion/particles/*.kpeffect
```

Format files in place:

```bash
./build/karma_particle_effect_format generated/fire_ray/particles/*.kpeffect
```

Check formatting without writing:

```bash
./build/karma_particle_effect_format --check generated/fire_ray/particles/*.kpeffect
```

CTest includes validator and formatter-check coverage for committed
`.kpeffect` files when tools and tests are enabled.

## Preview

Generated packages can be loaded by the preview example:

```bash
cmake --build build --target particles_generated_preview
KARMA_PARTICLE_STATS=1 ./build/examples/particles/generated_preview
```

With tools enabled, no arguments generates the built-in
`examples/particles/specs/fire_ray.kpspec.json` into `generated/fire_ray` and
loads it. The preview also accepts either a package directory or a spec path:

```bash
KARMA_PARTICLE_STATS=1 ./build/examples/particles/generated_preview generated/fire_ray
KARMA_PARTICLE_STATS=1 ./build/examples/particles/generated_preview \
  examples/particles/specs/magic_missile.kpspec.json
KARMA_PARTICLE_STATS=1 ./build/examples/particles/generated_preview \
  examples/particles/specs/energy_orb.kpspec.json
```

Pass `--scene` or `--scenery` to load a simple plane and colored cubes for
checking shimmer and distortion against visible materials.

When a spec path is passed, an optional second argument overrides the generated
package output directory. Package paths may also come from
`KARMA_GENERATED_PARTICLE_PACKAGE`.

## Runtime Model

`ParticleSystem` handles both generated emitters and generated beams:

1. Package import registers textures, `.kpeffect` files, and the prefab.
2. Prefab instantiation creates `ParticleEffectComponent` entities for emitter
   layers and an optional `ParticleBeamComponent` entity for continuous ribbons.
3. `ParticleSystem` resolves effect keys and submits particle emitters to the
   renderer.
4. `ParticleSystem` submits enabled beam components as
   `rendering::ParticleBeamGpuDesc`.
5. The Diligent backend expands beam path segments into camera-facing textured
   quads and draws them before normal particle passes.

Use `.kpeffect` for noisy, volumetric, or short-lived layers. Use
`ParticleBeamComponent` only for the continuous core/trail geometry that should
read as one connected ribbon.
