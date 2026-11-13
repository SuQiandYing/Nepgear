# Hooks Module Map

The public hook interfaces remain in `*.h`. Each top-level `*.cpp` is the
only translation unit for its subsystem and aggregates private implementation
slices from `internal/**/*.cppinc`.

`.cppinc` files must not be compiled independently. They intentionally share
private state, original API pointers, and helper functions with their aggregate
`.cpp`.

## Font hooks

- `font_hook.cpp` is the aggregate and namespace boundary for the imported
  SimpleFontHook subsystem.
- `hook_policy.cpp` owns API compatibility decisions, including TinkerBell.
- `../font/font_patcher.cpp` patches TTC/TTF/OTF name, cmap, codepage, and
  vertical metric tables.
- `internal/font/font_hooks_*.cppinc` contains creation, rendering, GDI+,
  DirectWrite, codepage redirect, text substitution, runtime, and install.
- `internal/font/model/` contains replacement caching, glyph virtualization,
  codepage epochs, and metric adjustment.
- `internal/font/queries/` contains font identity, enumeration, and metric APIs.
- `internal/font/engines/` contains the complete imported SimpleFontHook engine
  layer: Artemis, BGI, CatSystem2, DxLib, EntisGLS, Escude, KiriKiri, Majiro,
  Mirai, Ren'Py, Softpal, TinkerBell, TopCat (x86), TyranoScript,
  Unity/IL2CPP/Mono, and YU-RIS.
- `internal/font/engines/engine_file_hooks.cppinc` bridges the shared engine file
  dispatcher into Nepgear's file hooks. `file_hook.cpp` remains the single
  process-wide Win32 file Detour owner so engine redirects and Nepgear VFS
  compose without duplicate hooks.

## Locale emulation

- `locale_emulator.cpp` owns runtime staging, configuration validation and
  process relaunch, including the deferred Soft-Denchi helper.
- `internal/locale/lep_api.h` declares the fixed LEPB v4/416 and v2/204
  structures and the standard Windows process-creation ABI. The current
  process's runtime pair is required; the other pair is staged when available
  for LEP's cross-architecture child-process broker.

## Virtual filesystem

- `vfs.cpp` aggregates VFS state, path indexing, lifecycle, virtual file I/O,
  directory enumeration, and extraction.
- `file_hook.cpp` aggregates Win32 file API adapters and hook installation.

## Window title hooks

- `window_hook.cpp` aggregates synchronous API interception, window subclass
  enforcement, the optional WinEvent fallback, and installation.
- `internal/window/window_hook_title.cppinc` installs a same-thread window
  subclass after creation. It reapplies the configured Unicode title after
  direct `WM_SETTEXT` messages without polling or WinEvent dependency.
- `internal/window/window_hook_message.cppinc` installs an in-process,
  thread-local `WH_CALLWNDPROCRET` hook. It catches `WM_SETTEXT` after the
  real window procedure returns, even when a framework replaces the subclass
  chain or bypasses exported messaging APIs. The same module also covers
  framework forwarding through `CallWindowProc(A/W)` and
  `DefWindowProc(A/W)`.

## Shared utilities

- `utils.cpp` aggregates font loading, startup UI, logging, patch deployment,
  cleanup, and pattern scanning.

## Engine hooks

- `rioshiina_hook.cpp` aggregates RioShiina state, archive unpacking, virtual
  DVD handling, resource redirection, and hook installation.

Small independent hooks stay as ordinary `*.cpp` files. Third-party code under
`krkrz_sdk/` and generated proxy sources are intentionally not reorganized.
