# Linux Cross-Compilation — Status & Roadmap

The goal: produce the exact same `x86_64-pc-windows-msvc` Windows PE binary from a
Linux host, using `clang-cl + xwin + lld-link`. No engine porting, no
DirectX/OpenGL swap. This lets Linux devs build without a Windows VM / Visual
Studio install, and gives CI a Linux-native cross-build job.

Status: **feasible and roughly half done.** The MFC/ATL blocker is disproven
(the engine uses none), the tooling pipeline exists, and a large sweep of
source-level MSVC-permissiveness fixes has already landed. What remains is a
bounded set of source fixes (the long tail) plus the CMake/CI/devcontainer
plumbing.

This doc is the authoritative, consolidated reference. `tools/README.md` covers
the editor (clangd) tooling; `PROJECT.md` has the one-paragraph summary.

---

## 1. Current state

Measured with `tools/sweep-clang.py`, which runs `clang-cl -fsyntax-only` over
every translation unit in `compile_commands.json` (captured from a real MSBuild
run) and buckets failures by first error:

| | Translation units |
|---|---|
| Total engine TUs (excl. `sdk/` + `src/3rd party`) | 1781 |
| Stale DB entries (files deleted since capture) | ~147 |
| **Parse clean under clang-cl** | **~1387 (84.9% of live TUs)** |

Per-project clean rate after the sweep:

| Project | Clean |
|---|---|
| xrCDB, xrCPU_Pipe, xrXMLParser, ReShadeCompat | 100% |
| xrCore | 97% |
| xrSound | 96% |
| xrEngine | 95% |
| Layers (renderer) | 94% |
| xrPhysics | 90% |
| xrNetServer / xrParticles | 88% |
| xrGame | ~80% |

The ~1500-TU "wall" originally feared turned out to be a *small* number of
high-leverage root causes, now fixed (see §2). The remaining ~250 live TUs are a
genuine long tail of smaller categories (§3).

### Why not zero yet

Each category below is small, but they keep surfacing one behind another — fix
one blocker and the next one hiding behind it becomes the new first error. The
pattern is the same as everything already fixed: MSVC accepts something
permissively that clang (and any standards-conforming compiler) rejects, and the
fix is a small, additive, MSVC-compatible change.

### Exceptions are off — do not "fix" this

The engine deliberately builds with exceptions disabled: every `.vcxproj` sets
`<ExceptionHandling>false</ExceptionHandling>`, and `xrCore.h` `#error`s if
`_CPPUNWIND` is defined in release. `clang-cl`'s default `-fno-exceptions`
already matches this — **do not add `/EHsc` or `/EHa`** to any toolchain file or
sweep invocation. The one consequence (§3.1) is that `try`/`catch` is a hard
error under clang but only a pragma-suppressed warning (C4530) under MSVC.

---

## 2. What has been fixed (the high-leverage sweep)

These are the root causes behind the original "~1500 TUs don't parse clean".
All are small, additive, and MSVC-compatible:

1. **`#include` case-casing** — the codebase has thousands of `#include
   "WrongCase.h"` directives that only resolve because NTFS is case-insensitive.
   Fixed across `src/` (system headers: `math.h`, `Psapi.h`, `ShlObj.h`,
   `dplay/DPlay`, `TlHelp32.h`, …; project headers: `xrServer_Objects_ALife.h`,
   `NET_Compressor.h`, `weapon*.h`, `Level.h`, …) and the vendored D3DX10/11 SDK
   headers. Tooling: `tools/fix-include-case.py`.
   - **Policy**: fix include *text*, never rename files (renaming creates a
     recurring merge tax on every upstream pull).
2. **PCH include casing/resolution** — `stdafx.h` vs `StdAfx.h` mismatch meant
   xrGame/xrServerEntities TUs pulled `xrCore/stdafx.h` instead of
   `xrGame/StdAfx.h`, cascading `ENGINE_API`/`Device`/`THROW2` failures. Fixed
   per-project; `xrServerEntities/pch_script.h` and subdir files
   (`SteamAudio/`, `tri-colliderknoopc/`) now include the right PCH.

   > **Gotcha — never `../`-qualify a PCH include.** MSVC compiles these TUs with
   > `PrecompiledHeader=Use` (`/Yu"stdafx.h"`), which matches the *literal*
   > `#include "stdafx.h"` line as the PCH load point. Rewriting it to
   > `#include "../stdafx.h"` (an earlier attempt to help clang find the file
   > from a subdirectory) breaks that match and produces **C1010 "unexpected end
   > of file while looking for precompiled header"** on the Windows build, plus a
   > cascade of linker errors for the missing objects. Only the *casing* of the
   > PCH include is safe to change (MSVC matches it case-insensitively). The
   > clang-side "can't find the PCH" problem for subdir files is a *tooling*
   > concern (the project dir must be on the include path when `/Yu` is
   > stripped), not a source fix.
3. **Missing `template<>`** on explicit specializations — the
   `FACTORY_PTR_INSTANCIATE` macro (`Include/xrRender/FactoryPtr.h`) and
   `DEFINE_MIXED_DELEGATE_SCRIPT` (`xrGame/mixed_delegate.h`).
4. **Dependent-base lookup** — `_stl_extensions.h`, `_flags.h`,
   `PropertiesListTypes.h` (`NumericValue<T>`), etc. accessed inherited members
   unqualified; fixed with `using Base::member;` declarations.
5. **Missing `template`/`typename` disambiguators** — the `object_{saver,loader,
   cloner,comparer,destroyer}.h` member-template calls (`CHelper<T>::template
   save_data<...>`) and a luabind type-alias (`calc_has_arg.hpp`).
6. **`typename void` / `typename const` typos** in `xml_str_id_loader.h`,
   `ini_id_loader.h`, `graph_edge_inline.h`.
7. **Missing `extern`** on `xr_token difficulty_type_token[]` (`game_cl_single.h`).
8. **Vendored luabind** — guarded a bogus `std::function` explicit instantiation
   under `#if defined(_MSC_VER) && !defined(__clang__)`.

---

## 3. Remaining findings (the long tail)

Counts are first-error buckets from the last full sweep; each file may have more
errors behind its first. Representative files listed for orientation.

### 3.1 `try`/`catch` under `-fno-exceptions` — ~67 TUs

The script-binding headers (`xrServerEntities/xrServer_script_macroses.h` and
friends) expand macros containing `try`/`catch` for Lua error handling. MSVC
accepts this in release because the resulting C4530 warning is `#pragma`-disabled
and the handler is never actually reached; clang makes `try` under
`-fno-exceptions` a hard error.

**Fix**: wrap those `try`/`catch` blocks in `#ifdef XRAY_EXCEPTIONS` (the macro
is already 0 in release). Slightly invasive — each site needs a look to confirm
the code is genuinely dead in release.

### 3.2 Template-template parameter — ~66 TUs

`xrGame/a_star.h` (the A* pathfinding template) passes
`AStar::_Vertex<...>::_vertex` as a template-template argument, which clang
rejects without the `template` disambiguator. Deep template metaprogramming;
needs careful analysis per use rather than a blind mechanical edit. Affects most
of the AI/pathfinding TUs (`agent_manager.cpp`, `alife_*`, `map_location.cpp`,
…).

### 3.3 `expected-syntax` — ~22 TUs

`xrPhysics` (`PHGeometryOwner.cpp`, `PHShell.cpp`, `PHWorld.cpp`, `Physics.cpp`)
plus a few others. Same class as above (likely a shared header with a permissive
construct); needs a first-error drill-down to identify the common header.

### 3.4 Missing `template<>` in luabind registrations — ~14 TUs

The `script_*_script.cpp` files (`script_fvector_script.cpp`,
`script_reader_script.cpp`, `state_arguments_functions.cpp`, …) carry explicit
specializations for luabind `.def(...)` registration that need `template<>`.
Mechanical once the macro pattern is confirmed, but they're a different macro
than the ones already fixed.

### 3.5 Forward-declaration with nested name specifier — ~9 TUs

`xrGame` IK / animation files (`ActorAnimation.cpp`, `ActorCameras.cpp`,
`IKLimbsController.cpp`, `ik_*.cpp`). A `class Foo::Bar;` forward declaration
MSVC tolerates but clang rejects.

### 3.6 `no-template-named` — ~7 TUs

`agent_manager_planner.cpp`, `object_actions.cpp`, the `*_property_evaluators.cpp`
files — the GOAP planner. `property_evaluator_member.h` references
`_condition_type` unqualified (dependent name). Likely a `using`-declaration fix
like §2.4.

### 3.7 Small / one-off (each ≤6 TUs)

`taking the address of a temporary`, `cannot pass object of non-trivial type`,
`in-class initializer for static data member` (tbb), `cannot cast private base
class 'pure_relcase'`, `must explicitly qualify member function when taking its
address`, `missing 'typename' prior to dependent type name`, `right operand to ?
is void`, and a handful of stray file-not-found (`ximage.h`,
`script_game_object.h`) and macro pasting errors. Fix these opportunistically as
they surface.

### 3.8 Not yet attempted

The vendored 3rd-party C libraries (`sdk/`, `src/3rd party`) were deliberately
excluded from the engine-only sweep. They show the highest raw error counts and
are heavy with inline assembly / compiler-specific tricks — a different problem,
and lower priority since they're not edited day-to-day. See §5 Phase 2.

---

## 4. How to keep going

```
# measure the current clean rate (needs clang-cl on PATH + compile_commands.json)
python3 tools/sweep-clang.py --engine-only

# drill into one failing file's real clang error
# (use its compile_commands.json entry verbatim, with -fsyntax-only)

# find + fix case-casing issues (dry run first, then --apply)
python3 tools/fix-include-case.py
```

> `compile_commands.json` is currently captured from a Windows MSBuild run and
> re-anchored to the local checkout. After the CMake migration (§5 Phase 3) it
> will instead be generated by the build itself — same file, same consumers,
> no capture step.

The established loop: run `sweep-clang.py`, pick the top category, get the real
`clang-cl -fsyntax-only` error for a representative file (not clangd's condensed
"in included file" summary), apply the smallest standard-conforming fix that
doesn't change MSVC behavior, re-sweep. Every fix so far has been additive
(`template<>`, `template`, `typename`, `using Base::x;`, `extern`, case-corrected
include text) and therefore MSVC-safe by construction — no need to rebuild in
Windows to confirm.

---

## 5. Remaining work, in order

### Phase 1 — finish source-level compliance (the long tail)

Finish §3. No new tooling; the sweep tool is the gauge. Exit criterion: 100% of
live engine TUs parse clean under `clang-cl -fsyntax-only`. This is the real
work and the hard prerequisite for Phase 3.

### Phase 2 — 3rd-party assembly (blocker for actual linking)

For a *real* link (not just `-fsyntax-only`), the hand-written-assembly vendored
libs can't be ingested by clang:

- **Hard (hand-written asm)**: `LuaJIT` (x86/x64 asm in `lj_*.c` + the `buildvm`
  host tool) and `OpenSSL` (`bn_asm.c` and other per-arch asm). **Mitigation**:
  pre-build `.lib` files once on Windows, commit them to the repo, link
  statically — standard practice for cross-compilation projects.
- **Likely OK (intrinsics)**: `libtheora`, `libvorbis`, `libjpeg`, `mimalloc`
  (MMX/SSE intrinsics clang-cl supports); `OpenAL-soft` has a portable C
  fallback. Needs testing.

### Phase 3 — CMake migration

Translate the 55 `.vcxproj`/`Common.props` into `CMakeLists.txt`.
[OpenXRay's CMakeLists](https://github.com/OpenXRay/xray-16) is the reference
for the mechanical translation (same engine lineage, same vcxproj-shaped
problem) — ignore their Linux/OpenGL porting. `compile_commands.json` is the
golden source for verifying identical compile invocations.

**`compile_commands.json` capture becomes obsolete here.** The MSBuild-logger
capture pipeline (`tools/remap-compile-commands.py`, `tools/regen-compile-commands.sh`)
is transitional — it exists only to reconstruct per-file flags from the Windows
build. Once CMake builds, `CMAKE_EXPORT_COMPILE_COMMANDS=ON` + Ninja emits a
`compile_commands.json` directly, already pointed at the real paths (no
`/Development/` → `/repos/` remap) and already split per-TU. clangd and
`sweep-clang.py` then consume the CMake-generated DB instead. The file doesn't
go away — it's still what clangd and the sweep read — it just stops being a
hand-curated Windows capture. The `-fsyntax-only` sweep likewise becomes partly
redundant once CMake compiles everything for real, but stays useful as a fast
pre-link gate (especially during the Phase 2 asm-`.lib` phase, before full
linking works).

Toolchain file (`cmake/toolchain-x86_64-windows-msvc-clang.cmake`):

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_AR           llvm-lib)
set(CMAKE_RC_COMPILER  llvm-rc)

set(XWIN_ROOT $ENV{XWIN_ROOT})            # provided by the devcontainer image
set(CMAKE_SYSROOT ${XWIN_ROOT})

add_compile_options(/winsysroot${XWIN_ROOT})
add_compile_options(/clang:-fno-operator-names)   # `or`/`and` used as identifiers
# NOTE: do NOT add -fno-delayed-template-parsing — it ~9x's the error surface by
# forcing strict two-phase lookup on code MSVC itself parses lazily. Also do NOT
# add /EHsc — the engine builds with exceptions off (see §1).
```

Build:

```bash
cmake -B _build/cross -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-windows-msvc-clang.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build _build/cross
```

### Phase 4 — CI + devcontainer

See §6 (devcontainer) and §7 (CI). These are the last step — they wire the
Phase 3 build into a container image and a CI job.

---

## 6. Devcontainer spec (dev work)

A `.devcontainer` provides a Linux environment with `clang-cl`, `lld-link`
(`lld`), `cmake`, `ninja`, `python3`, and an `xwin` sysroot — everything needed
for both clangd editing (today) and cross-compilation (Phase 3+). The same image
is reused by the CI cross-build job, so the two never drift.

### `.devcontainer/Dockerfile`

```dockerfile
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        clang-18 lld-18 cmake ninja-build python3 git ca-certificates curl \
    && curl -L https://github.com/Jake-Shadle/xwin/releases/latest/download/xwin-x86_64-unknown-linux-musl.tar.gz \
        | tar xz -C /usr/local/bin \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# clang-cl / lld-link / llvm-lib / llvm-rc as first-class driver names
RUN ln -s /usr/bin/clang-18    /usr/local/bin/clang-cl  \
 && ln -s /usr/bin/lld-18      /usr/local/bin/lld-link  \
 && ln -s /usr/bin/llvm-ar-18  /usr/local/bin/llvm-lib  \
 && ln -s /usr/bin/llvm-rc-18  /usr/local/bin/llvm-rc

RUN xwin --accept-license splat --output /opt/xwin --arch x86_64
ENV XWIN_ROOT=/opt/xwin

RUN useradd -m builder
USER builder
WORKDIR /home/builder
```

### `.devcontainer/devcontainer.json`

```jsonc
{
  "name": "old-world-engine",
  "build": { "dockerfile": "Dockerfile" },
  "mounts": [
    // Keep the xwin sysroot + clangd index outside the (ephemeral) container.
    "source=xwin-cache,target=/home/builder/.xwin-cache,type=volume",
    "source=clangd-cache,target=/home/builder/.cache/clangd,type=volume"
  ],
  "customizations": {
    "vscode": {
      "extensions": ["llvm-vs-code-extensions.vscode-clangd"]
    }
  }
}
```

> Note: `cmake` and `lld` are intentionally provided by the devcontainer rather
> than the host `brew install` described in older notes — the container is the
> single source of truth for the toolchain, and it's what CI uses.

---

## 7. CI changes

Two additions, both on `ubuntu-latest` (the existing `msbuild.yml`
`windows-latest` build is unchanged and remains the source of shipped artifacts):

### 7.1 Cross-build job (add to `.github/workflows/msbuild.yml`)

```yaml
cross-build:
  runs-on: ubuntu-latest
  container:
    image: ghcr.io/<org>/xray-cross-build:latest   # built from .devcontainer/Dockerfile
  steps:
    - uses: actions/checkout@v4
      with:
        submodules: true
    - name: Configure
      run: cmake -B _build/cross -G Ninja \
             -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-windows-msvc-clang.cmake \
             -DCMAKE_BUILD_TYPE=Release
    - name: Build
      run: cmake --build _build/cross
```

This gates on "the engine actually cross-compiles" rather than replacing the
Windows build. Run it as a non-blocking check until it's green and stable, then
make it required.

### 7.2 Syntax-only sweep job (optional but cheap)

A `-fsyntax-only` job can run *before* the full CMake migration is done, as a
canary for the Phase 1 long tail:

```yaml
sweep:
  runs-on: ubuntu-latest
  container:
    image: ghcr.io/<org>/xray-cross-build:latest
  steps:
    - uses: actions/checkout@v4
    - name: Sweep
      run: |
        # compile_commands.json is git-ignored; generate or fetch it here
        python3 tools/sweep-clang.py --engine-only
```

Once the repo contains a committed, container-independent `compile_commands.json`
or a generator, this becomes a real regression gate.

### Image publication

The devcontainer image is built and pushed to GHCR (e.g. on a tag or via a
`workflow_dispatch`), then referenced by both `.devcontainer` and the CI jobs.

---

## 8. Decision log

- **Drop `-fno-delayed-template-parsing`** — measured ~9× fewer errors (36% vs
  4% clean on a 98-file sample). MSVC itself uses delayed parsing, so clang-cl's
  default is the faithful match. Revisit later as a hardening pass, not a
  blocker.
- **Exceptions stay off** — engine requirement; clang-cl default matches. See §1.
- **Fix include text, not filenames** — merge-tax avoidance. See §2.1.
- **`compile_commands.json` stays git-ignored** — machine-specific, tied to the
  xwin cache location; regenerated per machine (see `tools/README.md`).
