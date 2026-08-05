# Linux host editing (clangd) setup

This lets you edit the engine in Neovim on the Linux host with full clangd
support (real headers, real per-file flags, go-to-definition, completion),
while still building in Visual Studio inside the Windows container/VM exactly
as before. It does **not** change how the engine is built.

## Why this exists / where it's headed

The immediate goal was host-side editor tooling. The longer-term goal (not
done, not started as a build-system project) is to eventually get this fork
building with CMake on Linux too, the way
[OpenXRay/xray-16](https://github.com/OpenXRay/xray-16) already does for its
fork. That's a much bigger job than editor tooling - see
["Path to a CMake/Linux build"](#path-to-a-cmakelinux-build-the-real-goal)
below for what that would actually require and how this work contributes to
it.

## How it works

1. **`compile_commands.json` is captured from a real build**, not
   hand-written. In the Windows container, an MSBuild logger
   ([0xabu/MsBuildCompileCommandsJson](https://github.com/0xabu/MsBuildCompileCommandsJson))
   observes the actual `cl.exe` invocations during a full rebuild and emits
   them as a Clang-style compilation database. This captures real per-project
   include dirs, defines, and PCH settings without hand-parsing 55 `.vcxproj`
   files.

2. **`tools/remap-compile-commands.py`** turns that container-side capture
   into something clangd on the host can actually use:
   - Rewrites container paths (`C:\Users\Docker\source\repos\xray-monolith\...`)
     to the host checkout path, and backslashes to forward slashes.
   - Splits batched MSBuild invocations (one captured command listed **873
     source files** compiled in a single `cl.exe` call via `/MP` batching)
     into one compile-command entry per file - otherwise clangd would try to
     parse hundreds of sibling files for a single-file request.
   - Forces the driver token to `clang-cl` (the real captured path ends in
     `CL.exe`, which clang's driver-mode detection doesn't reliably match).
   - Injects Windows SDK/CRT headers from a local
     [`xwin`](https://github.com/Jake-Shadle/xwin) cache as `-isystem` paths -
     the real build gets these from the `INCLUDE` environment variable
     (`vcvarsall`-style), so they never appear as explicit `/I` flags in the
     capture.
   - Injects the repo-wide include roots declared in `src/Common.props`'s
     `<IncludePath>` (`src/3rd party`, `sdk/include/*`) for the same reason -
     IDE/project-level include dirs, not literal per-file flags.
   - Strips `/Yc`/`/Yu`/`/Fp` (precompiled header) flags - the referenced
     `.pch` is an MSVC-binary file clang can't read, and clangd's own
     PCH-reuse heuristics got confused by a `/Yu` flag pointing at a
     non-existent file. Dropping them just means every file re-parses
     `stdafx.h` as an ordinary header - correct, just slower.
   - **Gotcha, worth remembering**: clang-cl's CL-style argument parser
     silently drops any bare GNU-style flag (`-isystem`, `-fno-operator-names`)
     unless it's prefixed `/clang:`. This bit us twice.

3. **`.clangd`** (repo root, git-ignored - it has a machine-specific
   `xwin` cache path) adds a couple of universal compatibility flags,
   notably `/clang:-fno-operator-names` (see [Known gaps](#known-source-level-gaps-found-so-far)).

4. **`compile_commands.json`** itself (repo root) is also git-ignored - it's
   a generated, machine-specific artifact (absolute host paths, tied to your
   `xwin` cache location).

## One-time host setup

- `brew install xwin`, then `xwin --accept-license splat --output ~/.xwin-cache/splat`
  to fetch the real MSVC CRT + Windows SDK headers (no Wine involved - this is
  just a header/lib cache, used purely for clangd to resolve `#include
  <windows.h>` etc.).
- clangd via Mason (LazyVim's `lazyvim.plugins.extras.lang.clangd` extra is
  enabled in `lazyvim.json`; `clangd` is in the managed Mason tool list in
  `lua/plugins/mason.lua`).

## Regenerating `compile_commands.json`

You don't need to do this for routine edits to existing files - clangd
reparses on save. Regenerate when you add/remove a source file, or change
project-level build settings (new define, new include dir, new project).

1. In the Windows container (Developer PowerShell for VS, so `msbuild` is on
   `PATH`), from the repo root:
   ```powershell
   msbuild src\engine-vs2022.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 `
     "-logger:C:\path\to\MsBuildCompileCommandsJson\bin\Debug\netstandard2.0\CompileCommandsJson.dll;cctmp.json"
   Move-Item cctmp.json <shared-folder>\compile_commands.json -Force
   ```
   Must be a full rebuild (`/t:Rebuild`) - the logger only captures files it
   actually compiles, so an incremental build leaves gaps. This is the slow
   step; treat regeneration as an occasional maintenance task, not a
   per-commit one.

2. On the host:
   ```
   tools/regen-compile-commands.sh [path-to-shared-folder-compile_commands.json]
   ```

## Known source-level gaps found so far

Running clangd against the full capture (all ~2300 translation units) surfaced
real, pre-existing incompatibilities between what MSVC accepts permissively
and what standards-conforming compilers (clang, and eventually GCC for a
Linux build) require. None of these affect the Windows/MSVC build - MSVC
keeps working exactly as it always has regardless of what's fixed here. Each
one found so far turned out to be small and contained, but they clearly keep
turning up (fixing one reveals the next one hiding behind it) - it's not
a small, bounded list, so it's tracked here rather than promised as "done."

**Fixed:**

- `src/xrCore/xrMEMORY_POOL.h` renamed to `xrMemory_POOL.h` - both existing
  `#include` sites already used that casing; only worked on Windows because
  NTFS is case-insensitive.
- `src/xrCore/_stl_extensions.h`'s `xr_vector<T, allocator>` (and its `bool`
  specializations) accessed inherited `std::vector` members (`begin`, `end`,
  `capacity`, `reserve`, `erase`, `const_reference`, `reference`, `size_type`)
  unqualified. MSVC's permissive lookup finds these on a dependent base class;
  standards-conforming two-phase lookup (clang, gcc) doesn't. Fixed with
  `using inherited::foo;` / `using typename inherited::Bar;` declarations -
  purely additive, zero behavior change, MSVC-compatible too.
- `.clangd` adds `/clang:-fno-operator-names` - the codebase uses `or` as a
  plain identifier (e.g. `_flags.h`'s `SelfRef or(...)` method), valid under
  MSVC's non-conforming default, rejected by standard C++ unless told
  otherwise.
- ~3280 `#include "WrongCase.h"` directives across ~1469 files corrected to
  match actual on-disk casing (deliberately: **file casing was left alone,
  include directives were fixed instead** - since this fork pulls upstream
  changes periodically, renaming files would create an ongoing tax on every
  future merge, and new upstream files would just recreate the mismatch
  again; fixing include text is a normal one-time patch that merges like any
  other fork-maintenance change).

**Found, not yet fixed:**

- Explicit template specializations written without the `template<>` prefix
  (e.g. `Include/xrRender/FactoryPtr.h` around line 59-68) - another MSVC
  permissiveness clang doesn't allow. Same shape of fix as the others:
  contained, mechanical, low-risk. First hit via `src/xrGame/ui/UIFrameWindow.cpp`.
- Broader picture: after the include-casing fix, a full re-sweep showed
  **75 of 1766 engine translation units parse clean** (roughly flat vs. 88
  before that fix - fixing one blocker just exposes what's behind it, not a
  regression). The remaining ~1500 files have at least one real
  MSVC-vs-standard incompatibility somewhere in their include chain. Expect
  more categories like the ones above (dependent-base lookup, non-standard
  specialization syntax, case sensitivity) as more of the codebase gets
  exercised - and, since a lot of it is decades-old, possibly some categories
  not seen yet.
- Not investigated at all: the vendored 3rd-party C libraries (LuaJIT,
  OpenSSL's `bn_asm.c`, Theora/Vorbis/jpeg codec libs, OpenAL-soft). These
  showed the highest raw error counts in the sweep (LuaJIT's `lj_crecord.c`
  alone: 1354) but are heavy with inline assembly and compiler-specific
  tricks - plausibly a different, harder problem than the engine's own code,
  and lower priority since you don't edit vendored code day-to-day.

**How to keep going:** fix these opportunistically - when a file you're
actually working on is noisy in clangd, dig into *that* file's blocker rather
than trying to sweep the whole repository to zero. The pattern so far: get
the real underlying clang error (not just clangd's condensed "in included
file" summary - run clang-cl directly with `-fsyntax-only` against the same
compile-database entry to see the actual file:line), then apply the smallest
standard-conforming fix that doesn't change MSVC's behavior.

## Path to a CMake-based Windows cross-compile (the real goal)

The goal is purely to get Linux devs out of needing a Windows VM/VS install
*to build* - i.e. real cross-compilation, producing the exact same
`x86_64-pc-windows-msvc` binary (DirectX, Win32 APIs, all untouched), just
built from a Linux host. No engine porting, no DirectX/OpenGL swap,  none of that is needed or wanted here.

This tooling work directly serves that goal. Concretely, the pieces line up
like this:

- **The actual CMake migration**: translating 55 `.vcxproj`/`Common.props`
  into `CMakeLists.txt` - per-project include dirs, defines, and PCH settings.
  [OpenXRay's CMakeLists structure](https://github.com/OpenXRay/xray-16) is
  still worth diffing against for *how to structure the mechanical
  translation* (same engine lineage, same vcxproj-shaped problem) - just
  ignore the parts of their fork that are about porting to native
  Linux/OpenGL, which don't apply here.
- **A CMake toolchain file targeting `clang-cl` + `lld-link` against the
  `xwin` sysroot** - this is the same `xwin` cache already set up for clangd
  in this session, just used for real compilation and linking instead of
  editor parsing. `xwin splat` already produces the `.lib`s alongside the
  headers, so the missing piece is wiring a CMake toolchain file to point at
  them (`crt/lib`, `sdk/lib/um`, `sdk/lib/ucrt`) instead of a real MSVC
  install. No Wine involved anywhere in this - `clang-cl`/`lld-link` produce
  a real Windows PE binary directly on Linux.
- **The source-level MSVC-permissiveness issues found in this session become
  load-bearing, not optional.** clangd's parse-only check tolerates some
  sloppiness; an actual `clang-cl` compile+link will not. Every gap listed
  above (and whatever else turns up) needs to be genuinely fixed, not just
  parsed around, before real cross-compilation can work.
- **The vendored 3rd-party libraries** need to actually build under
  `clang-cl`, which is a higher bar than clangd's syntax check tolerated
  (LuaJIT's `buildvm` step and inline-asm-heavy code, OpenSSL's hand-written
  asm, etc. are the likely pain points) - unrelated to whether they run on
  Linux, since the output is still a Windows binary either way.

