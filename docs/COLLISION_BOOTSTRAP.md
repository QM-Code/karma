# Collision Bootstrap

This file is the handoff for the current collision/contact ECS pass.

## What Exists Now

There are now three related but separate pieces of collision state:

1. overlap events
2. solid contact events
3. ground contact state

They are intentionally not collapsed into one API.

## ECS Surface

Overlap events:

- [collision_events.h](/home/irie/Documents/karma/include/karma/components/collision_events.h)

Solid contact events:

- [contact_events.h](/home/irie/Documents/karma/include/karma/components/contact_events.h)

Grounded/support state:

- [ground_contact.h](/home/irie/Documents/karma/include/karma/components/ground_contact.h)

Game-facing fixed-step hook:

- [game_interface.h](/home/irie/Documents/karma/include/karma/app/game_interface.h)

## Runtime Systems

Overlap diffing system:

- [collision_event_system.cpp](/home/irie/Documents/karma/src/collision/collision_event_system.cpp)

Physics-driven contact + grounded updates:

- [physics_system.cpp](/home/irie/Documents/karma/src/physics/physics_system.cpp)

Engine integration:

- [engine_app.cpp](/home/irie/Documents/karma/src/app/engine_app.cpp)

## Current Behavior

### Overlap events

These are built by diffing overlap sets each fixed tick.

They are good for:

- triggers
- zones
- pickups
- "what am I inside right now?"

They are not intended to provide physical contact geometry.

### Contact events

These are physics-driven and expose:

- `other`
- `point`
- `normal`
- enter / stay / exit phase through the event buffers

This is the path to use when gameplay needs actual collision information rather than just overlap state.

### Ground contact

`GroundContactComponent` currently exposes:

- `grounded`
- `entered`
- `exited`
- `support_entity`
- `point`
- `normal`

Support details are strongest on the player-controller path and now also resolved for box rigid bodies through a downward support probe.

## Current Limits

Ground support probing is only implemented for box rigid bodies right now.

That means:

- player controller grounding: good
- box rigid body grounding: reasonable
- sphere/capsule rigid body grounding: not done yet

Also note:

- overlap events are still query-based, not contact-manifold-based
- contact events are the right place for physical collision data

## Example To Read First

The main example for this subsystem is:

- [collision_events_example.cpp](/home/irie/Documents/karma/examples/collision_events_example.cpp)

What it shows:

- drivable player
- trigger overlap enter/exit
- solid contact info
- grounded state
- just-landed / just-left-ground
- jump with `Space`

## Validation

Commands used during this pass:

```bash
cmake --build build --target karma_collision_events_example -j2
timeout 5s ./build/karma_collision_events_example
```

Expected result here:

- build succeeds
- startup succeeds
- runtime stops at `GLFW failed to initialize` in this headless environment

## Recommended Next Steps

Best next moves:

1. extend ground support resolution to sphere/capsule rigid bodies
2. validate contact point/normal quality on more contact pairs
3. keep overlap events and contact events architecturally separate
4. add higher-level helpers only after the low-level event data is trusted

## Design Guidance

Keep this distinction:

- overlap events answer "what am I overlapping?"
- contact events answer "what did I physically hit?"
- ground contact answers "am I supported and by what?"

Do not fold those into one catch-all callback API unless there is a very strong reason.
