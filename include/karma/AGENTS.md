# Public Header Rules

New public headers must live at the root `karma/*.h` surface and be included
using that same public path, for example:

- `karma/components.h`
- `karma/physics.h`
- `karma/visual.h`

Do not add forwarding headers for old include paths.

Public headers should expose data contracts and narrow subsystem APIs. Keep
backend internals and large implementation records in `src/` unless users
actually need them.

UI provider adapters live under `src/features/ui/<provider>`. Their public APIs
belong in provider-specific headers such as `karma/ui_imgui.h` and
`karma/ui_rmlui.h`, around the generic `karma/app.h` UI layer contract.
