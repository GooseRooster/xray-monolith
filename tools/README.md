# tools/ — Linux-host developer tooling

Host-side tooling for working on the engine from Linux. Two purposes:

1. **Editing** — full clangd support (real headers, per-file flags,
   go-to-definition) while still building in a Windows VM/container as before.
2. **Source-compliance fixing** — measuring and shrinking the gap between what
   MSVC accepts and what `clang-cl` (and eventually the CMake cross-compile)
   requires.

For the *roadmap* — the actual Linux→Windows cross-compile, the remaining
source-level findings, and the CMake/CI/devcontainer plan — see
[`docs/linux-cross-compile.md`](../docs/linux-cross-compile.md). This README
covers only the tools themselves.

## Files

| File | Purpose |
|---|---|
| `remap-compile-commands.py` | Convert a container-captured `compile_commands.json` into one clangd on the host can use (path remap, `/MP` batch split, xwin include injection, PCH-flag stripping). |
| `regen-compile-commands.sh` | Wrapper: run `remap-compile-commands.py` against a freshly captured DB. |
| `sweep-clang.py` | Run `clang-cl -fsyntax-only` over the whole DB; report per-project clean counts + first-error categories. The "how far along are we" gauge. |
| `fix-include-case.py` | Find and case-correct `#include` directives that only resolve on case-insensitive NTFS. |

## One-time host setup (editing)

- `xwin --accept-license splat --output ~/.xwin-cache/splat` — fetches the real
  MSVC CRT + Windows SDK headers/libs used by clangd (and later by the
  cross-compile). No Wine involved.
- clangd via Mason (the `lazyvim.plugins.extras.lang.clangd` extra is enabled in
  `lazyvim.json`).
- `.clangd` (repo root, git-ignored) holds universal compatibility flags, most
  importantly `/clang:-fno-operator-names` (the codebase uses `or`/`and` as
  plain identifiers).

## Regenerating `compile_commands.json`

You don't need this for routine edits (clangd reparses on save). Regenerate when
you add/remove a source file or change project-level build settings. The DB is
git-ignored — it's machine-specific (absolute paths, tied to your xwin cache).

1. In the Windows container (Developer PowerShell for VS), from the repo root:
   ```powershell
   msbuild src\engine-vs2022.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 `
     "-logger:C:\path\to\MsBuildCompileCommandsJson\bin\Debug\netstandard2.0\CompileCommandsJson.dll;cctmp.json"
   Move-Item cctmp.json <shared-folder>\compile_commands.json -Force
   ```
   Must be a full rebuild (`/t:Rebuild`) — the logger only captures files it
   actually compiles, so an incremental build leaves gaps.
2. On the host:
   ```
   tools/regen-compile-commands.sh [path-to-shared-folder-compile_commands.json]
   ```

## Measuring source compliance

```
# full engine sweep (excludes sdk/ and src/3rd party)
python3 tools/sweep-clang.py --engine-only

# quick smoke test
python3 tools/sweep-clang.py --engine-only --sample 40

# save per-file results for drill-down
python3 tools/sweep-clang.py --engine-only --out results.json
```

`sweep-clang.py` requires `clang-cl` on `PATH` and a generated
`compile_commands.json`. It re-anchors the DB's absolute paths to the current
checkout automatically (so it survives a repo move), and intentionally does
**not** enable exceptions — the engine builds with them off (see
`docs/linux-cross-compile.md`).

The workflow when fixing the long tail:

1. `sweep-clang.py --engine-only`, pick the top category.
2. Get the *real* clang error for a representative file (not clangd's condensed
   "in included file" summary) — run `clang-cl -fsyntax-only` against the same
   compile-database entry.
3. Apply the smallest standard-conforming fix that doesn't change MSVC behavior
   (`template<>`, `template`, `typename`, `using Base::x;`, `extern`, corrected
   include text).
4. Re-sweep.

## Fixing include-casing

```
# dry run (report only)
python3 tools/fix-include-case.py

# apply
python3 tools/fix-include-case.py --apply

# restrict to a subtree
python3 tools/fix-include-case.py --root src/xrGame --apply
```

Safety: PCH includes (`stdafx.h`/`StdAfx.h`) are skipped — their correct casing
is per-project and can't be resolved by filesystem globbing alone. A fix is
applied only when every candidate search root agrees on the same on-disk casing.

## clang-cl gotchas worth remembering

- clang-cl's CL-style argument parser **silently drops** bare GNU flags
  (`-isystem`, `-fno-operator-names`) unless prefixed `/clang:`. This has bitten
  the setup twice.
- clang-cl defaults to `-fno-exceptions` (matching the engine), but its default
  is otherwise MSVC-compatible delayed template parsing — **don't** add
  `-fno-delayed-template-parsing` (it ~9×'s the error surface; see the doc).
- The engine builds with exceptions off; `xrCore.h` `#error`s if you enable them.
- Do **not** change a PCH include to a relative path (`../stdafx.h`) to "help"
  clang find it from a subdir — MSVC's `/Yu"stdafx.h"` needs the literal
  `#include "stdafx.h"` line and will C1010 otherwise (see
  `docs/linux-cross-compile.md` §2). Only the casing is safe to change; the
  clang-side path resolution is a tooling concern.
