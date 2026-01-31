RmlUi + Diligent UI Pipeline Notes (Karma)
==========================================

This note captures the fixes and alignment work that made the RmlUi demo render
text correctly in the Karma Diligent backend. It is intended for future
debugging and for AI assistants reviewing the project history.

Symptom
-------
- RmlUi demo rendered glyphs as squares or solid blocks.
- ImGui demo rendered correctly.
- Later, after some changes, RmlUi UI stretched across the screen.

Reference Implementations Compared
----------------------------------
- DiligentEngineXRmlUI (external example)
- renderer_diligent.cpp (Karma-side reference from another project)

Key Differences Observed
------------------------
1) Pipeline Layout
   - DiligentEngineXRmlUI uses separate color and textured pixel shaders,
     and separate PSOs for scissor on/off.
   - The original Karma UI path used a single textured shader and a single PSO.

2) Resource Layout / Samplers
   - The reference uses a dynamic texture SRV and an immutable sampler
     named "_texture_sampler".
   - Karma previously used a mutable SRV and a static sampler with a different name.

3) Matrix Convention
   - The reference uses row-major matrices and `mul(float4, matrix)` in the VS.
   - Karma had been toggled between matrix layouts, which caused the UI to stretch.

4) Font Loading
   - The RmlUi demo loaded the font by a relative path that could fail when
     running from the build directory.
   - Font loading was made robust by resolving the asset path.

Root Cause & Final Fix
----------------------
The core fix was to mirror the DiligentEngineXRmlUI rendering pipeline closely:

1) Split pixel shaders:
   - Color PSO: no texture sampling, outputs vertex color.
   - Texture PSO: samples atlas texture and multiplies by vertex color.

2) Create four PSOs:
   - Color / Color+Scissor / Texture / Texture+Scissor.
   - Selected per draw command based on texture presence and scissor flag.

3) Resource layout for textured PSOs:
   - Use a dynamic SRV named "g_Texture".
   - Use an immutable sampler named "g_Texture_sampler".
   - IMPORTANT: use static storage for the resource layout descriptors
     (variables + samplers). A crash occurred when these were stack-allocated,
     leading to "ResourceLayout.Variables[0].Name must not be null".

4) Matrix convention:
   - Use `row_major float4x4` in the shader and `mul(float4, matrix)` in the VS.
   - Upload the projection matrix without transposing.
   - This removed the UI stretching while keeping text rendering correct.

5) Font path resolution:
   - The RmlUi demo resolves `examples/assets/Roboto-Black.ttf` relative to
     the current working directory to avoid silent font fallback.

Where the changes live
----------------------
- UI backend: `src/renderer/backends/diligent/backend_ui.cpp`
- UI backend fields: `include/karma/renderer/backends/diligent/backend.hpp`
- RmlUi demo font resolution: `examples/rmlui_ui_demo.cpp`

Minimal runtime log kept
------------------------
- A single info log is emitted once: "Karma UI: pipeline created."
  to confirm the UI PSOs were built.

If this breaks again
--------------------
1) Verify the atlas texture SRV is created and bound.
2) Check the UI PSO resource layout uses correct names:
   - "g_Texture" and "g_Texture_sampler".
3) Confirm matrix layout and VS `mul` order matches this note.
4) Confirm font path resolution still finds the font file.
