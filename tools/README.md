# Linux host editing (clangd) setup

This lets you edit the engine in Neovim on the Linux host with full clangd
support (real headers, real per-file flags, go-to-definition, completion),
while still building in Visual Studio inside the Windows container/VM exactly
as before. It does **not** change how the engine is built.

## Why this exists / where it's headed

The immediate goal was host-side editor tooling. The longer-term goal is to
**cross-compile from Linux to Windows PE** — producing the exact same DirectX
binary, just built from the Linux host — using the
`clang-cl + xwin + lld-link` toolchain.

This is now confirmed **feasible** (July 2025 investigation):
- **The engine does not actually use MFC/ATL** — the MSVC workload
  requirement in `AGENTS.md` is legacy/editor-only, not needed by the runtime.
  Every `.vcxproj` declares `UseOfMfc=false` or omits it entirely. The ~3280
  `AFX_*` tokens in the codebase are ClassWizard include-guard conventions
  (`AFX_BLENDER_H__`), not MFC API calls. The splash dialog and crash reporter
  use raw Win32 `CreateDialog`/`DialogBox`.
- The tooling pipeline (`xwin`, `clang-cl`, `compile_commands.json` remapping)
  is already 80% in place from the clangd setup — the missing piece is making
  the code actually *compile* under clang rather than just *parse* for LSP.
- See ["Path to a CMake-based Windows cross-compile"](#path-to-a-cmake-based-windows-cross-compile-the-real-goal)
  for the full roadmap, approach comparison, and interim build options.

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

**3rd-party libraries: cross-compilation impact**

For an actual clang-cl cross-compile (not just clangd parsing), these
libraries become a genuine blocker — clang can't ingest MSVC inline assembly.
Two tiers:

- **Hard (hand-written asm)**: `LuaJIT` (x86/x64 asm in `lj_*.c` +
  `buildvm` host tool), `OpenSSL` (`bn_asm.c` and other per-arch asm files).
  The pragmatic mitigation is to pre-build `.lib` files once on Windows,
  commit them to the repo, and link statically — many real cross-compilation
  projects do exactly this.

- **Likely OK (intrinsics)**: `libtheora`, `libvorbis`, `libjpeg`,
  `mimalloc` — these use MMX/SSE intrinsics that clang-cl supports. Need
  testing but expected to compile as-is. `OpenAL-soft` already has a
  portable C fallback.

**How to keep going:** fix these opportunistically - when a file you're
actually working on is noisy in clangd, dig into *that* file's blocker rather
than trying to sweep the whole repository to zero. The pattern so far: get
the real underlying clang error (not just clangd's condensed "in included
file" summary - run clang-cl directly with `-fsyntax-only` against the same
compile-database entry to see the actual file:line), then apply the smallest
standard-conforming fix that doesn't change MSVC's behavior.

## Path to a CMake-based Windows cross-compile (the real goal)

The goal is purely to get Linux devs out of needing a Windows VM/VS install
*to build* — i.e. real cross-compilation, producing the exact same
`x86_64-pc-windows-msvc` binary (DirectX, Win32 APIs, all untouched), just
built from a Linux host. No engine porting, no DirectX/OpenGL swap — none of
that is needed or wanted here.

### Feasibility: confirmed viable

The MFC/ATL investigation (July 2025) removed the single biggest blocker most
people assume with MSVC-to-clang migration:

- **Zero MFC/ATL usage in the engine runtime.** All `.vcxproj` files that
  specify `UseOfMfc` set it to `false`. The codebase uses raw Win32
  (`CreateDialog`, `DialogBox`, `DIALOGEX` resources) for its two dialogs
  (splash screen and crash reporter). The `WinResRC.h` shim (referenced by
  `.rc` files) explicitly avoids MFC's `afxres.h`.
- **xwin** provides CRT + Windows SDK headers *and* `.lib` files — not just
  headers for clangd, but linkable import libraries for `clang-cl`/`lld-link`.
  The same `xwin splat` already used for editor tooling produces everything
  needed for actual compilation.
- **The tooling pipeline is 80% in place:** `compile_commands.json` capture,
  path remapping for `clang-cl`, `xwin` sysroot. The remaining 20% is making
  the code genuinely compile under clang (not just parse for LSP).

### Three approaches compared

| Approach | Source fixes | Wine needed | True cross-compile | Verdict |
|----------|-------------|-------------|-------------------|---------|
| **A: clang-cl + xwin + lld-link** | Yes (~1500 TU fixes) | No | Yes (Linux native) | **Target** |
| B: msvc-wine | None | Yes | Partial (Wine layer) | Fallback |
| C: Windows container (dockur/windows) | None | No (KVM VM) | No (full Windows VM) | Interim |

**Approach A (recommended)** is the end goal — pure Linux-native cross-compilation
yielding a Windows PE binary. It aligns with the clangd work already in
progress (every file fixed for LSP is one step closer to real compilation).

**Approach B** runs the actual MSVC toolchain (`cl.exe`, `link.exe`) under
Wine on Linux — zero source changes, 100% MSVC compatibility. Fragile in
practice (mspdbsrv issues, debug builds), but a valid fallback if Phase 1
reveals intractable clang incompatibilities.

**Approach C** is covered in [Interim build options](#interim-build-options-windows-containers) below.

### Phase breakdown

**Phase 1 — Source-level compliance (the real work):**
The ~1500 TUs with MSVC-vs-standard incompatibilities. Same categories
already identified in the clangd gaps: missing `template<>`, dependent-base
lookup, `or`/`and` as identifiers, and possibly new categories not yet seen.
Each file fixed for clangd moves you closer to actual compilation.

**Phase 2 — 3rd-party assembly:**
Pre-build `LuaJIT` and `OpenSSL` `.lib` files on Windows once, commit to
repo, link statically. Other 3rd-party libraries (libjpeg, vorbis, theora,
mimalloc) use MSVC intrinsics that clang-cl supports — expected to compile.

**Phase 3 — CMake migration:**
Translate 55 `.vcxproj`/`Common.props` into `CMakeLists.txt`.
[OpenXRay's CMakeLists structure](https://github.com/OpenXRay/xray-16) is
the reference for the mechanical translation (same engine lineage, same
vcxproj-shaped problem) — ignore their Linux/OpenGL porting, which doesn't
apply here. The existing `compile_commands.json` is the golden source of
truth for verifying identical compile invocations.

**Phase 4 — Container + CI:**
Containerfile-based cross-build image (see below), CMake toolchain file
wired to `xwin` sysroot, `clang-cl` + `lld-link` invocation. Add a CI job
on `ubuntu-latest` alongside the existing `windows-latest` CI.

### Toolchain reference

A CMake toolchain file targeting `clang-cl` + `lld-link` against the `xwin`
sysroot:

```cmake
# cmake/toolchain-x86_64-windows-msvc-clang.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_AR           llvm-lib)
set(CMAKE_RC_COMPILER  llvm-rc)

set(XWIN_ROOT $ENV{HOME}/.xwin-cache/splat)
set(CMAKE_SYSROOT ${XWIN_ROOT})

add_compile_options(/winsysroot${XWIN_ROOT})
add_compile_options(/clang:-fno-operator-names)
add_compile_options(/clang:-fno-delayed-template-parsing)
```

Cross-build image (`tools/CrossBuild.Containerfile`):

```dockerfile
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-18 lld-18 cmake ninja-build python3 git ca-certificates curl \
    && curl -L https://github.com/Jake-Shadle/xwin/releases/latest/download/xwin-x86_64-unknown-linux-musl.tar.gz \
    | tar xz -C /usr/local/bin \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/clang-18    /usr/local/bin/clang-cl  \
    && ln -s /usr/bin/lld-18    /usr/local/bin/lld-link \
    && ln -s /usr/bin/llvm-ar-18 /usr/local/bin/llvm-lib \
    && ln -s /usr/bin/llvm-rc-18 /usr/local/bin/llvm-rc

RUN xwin --accept-license splat --output /opt/xwin --arch x86_64
ENV XWIN_ROOT=/opt/xwin

RUN useradd -m builder
USER builder
WORKDIR /home/builder
```

Build invocation:

```bash
cmake -B _build/cross -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-windows-msvc-clang.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build _build/cross
```

CI integration (add to `.github/workflows/msbuild.yml`):

```yaml
cross-build:
  runs-on: ubuntu-latest
  container:
    image: ghcr.io/oldworld/xray-cross-build:latest
  steps:
    - uses: actions/checkout@v4
    - run: cmake -B _build/cross -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-windows-msvc-clang.cmake
    - run: cmake --build _build/cross
```

### Interim build options (Windows containers)

Until cross-compilation is working, there are two ways to build without a
full Windows VM on your desktop:

**dockur/windows** — Runs a full Windows VM inside a podman/docker container
on Linux via KVM/QEMU. Accessible via web browser (port 8006) or RDP (3389).
You'd install Visual Studio inside the guest, bind-mount the repo, and build
as normal. Requires KVM on the host. Essentially a VM with a container
wrapper — convenient setup, but not fundamentally different from your
existing VM workflow performance-wise.

**GitHub Actions** — The existing `windows-latest` CI runner already builds
DX11 and DX11-AVX on every push/PR. For a quick build without local tooling,
push a branch and pull the artifact.

**msvc-wine** — Runs the actual MSVC `cl.exe`/`link.exe` under Wine. Zero
source changes needed, full MSVC compatibility. Useful as a stopgap for
local builds if dockur/windows KVM is unavailable, but fragile (mspdbsrv
crashes, non-trivial setup).

