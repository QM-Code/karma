# Known Issues

This page tracks confirmed runtime and platform issues that are useful to keep
visible while the engine is moving quickly.

## Linux NVIDIA Vulkan Mouse-Click Present Stalls

Status: known platform/WSI issue, documented on 2026-06-20.

On some Linux NVIDIA Vulkan setups, including a tested GTX 970 environment, the
renderer can hitch when mouse buttons are clicked in a Vulkan window. The issue
has been observed in the `rendering_grass_field` example with 50,000 instances,
but the diagnostics do not point to grass rendering or input processing as the
cause.

Observed diagnostics after renderer warm-up:

- Mouse-button frames are cheap on the game thread. In one uncapped 50k grass
  run, 108 click frames averaged `events=0.033ms` and `input=0.005ms`.
- No post-warm-up resource creates, pipeline creates, or extra instance uploads
  occurred during the click test.
- The large stalls landed in Vulkan WSI work. The same run saw
  `vkQueuePresentKHR`/present diagnostics such as `queue_present=786.249ms` and
  acquire diagnostics such as `acquire_total=156.839ms` with the wait in the
  acquire fence path.
- The swapchain was using `VK_PRESENT_MODE_IMMEDIATE_KHR` with
  `sync_interval=0`, so immediate present does not fully avoid the stall on the
  affected platform.

The likely cause is interaction between Linux window-system mouse-button
handling, the compositor/window manager, and the NVIDIA Vulkan WSI path. Mouse
button presses can trigger pointer/focus/grab behavior that keyboard input does
not, and Vulkan present/acquire calls are allowed to block depending on the
native presentation engine.

Current mitigations:

- Default apps already CPU-pace frame starts at 60 FPS. For dense examples,
  lower the cap if present/acquire stalls remain visible:

  ```bash
  KARMA_ENGINE_FRAME_PACING_FPS=30 ./build/examples/rendering/grass_field 50000
  ```

- Keep immediate present for low-latency testing by using the renderer API
  present-mode setting or a direct diagnostic override:

  ```bash
  KARMA_DILIGENT_PRESENT_MODE=immediate \
  KARMA_ENGINE_VSYNC=0 \
  ./build/examples/rendering/grass_field 50000
  ```

- For uncapped diagnostics, disable CPU pacing and capture both engine and
  Diligent present timing:

  ```bash
  KARMA_ENGINE_FRAME_PACING_FPS=0 \
  KARMA_ENGINE_FRAME_DIAG=1 \
  KARMA_ENGINE_FRAME_DIAG_THRESHOLD_MS=8 \
  KARMA_DILIGENT_PRESENT_DIAG=1 \
  KARMA_DILIGENT_PRESENT_DIAG_THRESHOLD_MS=4 \
  ./build/examples/rendering/grass_field 50000
  ```

- Test platform variables when possible: fullscreen or borderless modes,
  compositor disabled, a different window manager, Wayland vs X11, and a newer
  NVIDIA driver.

Engine-side follow-up, if this becomes a priority again:

- Make mouse-click present suppression render-thread-aware instead of counting
  only game frames.
- Add stronger renderer backpressure or pacing when the game thread outruns the
  render thread, so present statistics and present-skip windows line up with
  actual rendered frames.
