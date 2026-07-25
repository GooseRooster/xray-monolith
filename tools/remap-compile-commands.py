#!/usr/bin/env python3
"""Convert a compile_commands.json captured inside the Windows build container
into one usable by clangd on the Linux host.

- Rewrites backslash paths to forward slashes (avoids POSIX shell-token
  backslash-eating when clangd re-splits the "command" string, and Windows/cl.exe
  accept forward slashes as path separators just fine).
- Remaps the container repo root to the host repo root.
- Splits batched MSBuild invocations (multiple source files compiled in a single
  cl.exe call, e.g. via /MP batching) into one compile-command entry per file,
  keeping only that file's own name as the positional source argument so clangd
  doesn't try to parse hundreds of sibling files for a single-file request.
"""
import argparse
import json
import re

SOURCE_EXTS = (".c", ".cc", ".cpp", ".cxx")

# Windows SDK / MSVC CRT headers, via `xwin` (no explicit /I for these appear in
# captured commands - MSVC resolves them from the INCLUDE env var at build time).
# Inserted right after the driver token (NOT appended at the tail) so the real
# source file argument stays unambiguously the last positional token - clang-cl's
# input-file detection otherwise picks up a trailing bare `-isystem` path value
# as if it were the file to compile.
# clang-cl (--driver-mode=cl) parses args with CL's own slash-style rules, which
# don't recognize bare "-isystem" - it gets silently dropped along with its value.
# The "/clang:" prefix forces each token straight through to the cc1 frontend,
# bypassing CL-style parsing.
def _clang_passthrough(flag: str, value: str) -> list[str]:
    return [f"/clang:{flag}", f"/clang:{value}"]


XWIN_SYSTEM_INCLUDES = [
    *_clang_passthrough("-isystem", "/var/home/gooze/.xwin-cache/splat/crt/include"),
    *_clang_passthrough("-isystem", "/var/home/gooze/.xwin-cache/splat/sdk/include/ucrt"),
    *_clang_passthrough("-isystem", "/var/home/gooze/.xwin-cache/splat/sdk/include/um"),
    *_clang_passthrough("-isystem", "/var/home/gooze/.xwin-cache/splat/sdk/include/shared"),
]


def repo_wide_includes(host_root: str) -> list[str]:
    """Mirrors src/Common.props's <IncludePath> (VC++ Directories entry) - these
    directories are on the real build's search path via project/IDE-level config,
    not literal per-file /I flags, so they never show up in the captured commands
    (e.g. xrCore/xr_resource.h's `#include <fast_dynamic_cast/fast_dynamic_cast.hpp>`
    only resolves via `src/3rd party` being on this path)."""
    roots = [
        f"{host_root}/src/3rd party",
        f"{host_root}/src/3rd party/icu/include",
        f"{host_root}/sdk/include",
        f"{host_root}/sdk/include/OpenAutomate",
        f"{host_root}/sdk/include/nvapi",
        f"{host_root}/sdk/include/dxsdk",
    ]
    args = []
    for r in roots:
        args += ["/I", r]
    return args


def split_command_line(command: str) -> list[str]:
    """Minimal Windows-style command-line tokenizer (quotes group tokens,
    no escape handling beyond that - sufficient for these MSBuild-emitted lines)."""
    tokens = []
    i, n = 0, len(command)
    while i < n:
        while i < n and command[i] == " ":
            i += 1
        if i >= n:
            break
        start = i
        buf = []
        in_quotes = False
        while i < n and (in_quotes or command[i] != " "):
            if command[i] == '"':
                in_quotes = not in_quotes
                i += 1
                continue
            buf.append(command[i])
            i += 1
        tokens.append("".join(buf))
    return tokens


def is_source_arg(tok: str) -> bool:
    return not tok.startswith(("/", "-")) and tok.lower().endswith(SOURCE_EXTS)


def is_pch_arg(tok: str) -> bool:
    # /Yc"stdafx.h" (create), /Yu"stdafx.h" (use an existing .pch), /Fp"...pch"
    # (the .pch path itself). These only matter for real MSVC compile-speed;
    # the referenced .pch is an MSVC-binary file clang can't read anyway, and
    # clangd's own PCH-reuse heuristics for /Yu get confused without a real one
    # (observed: spurious "incomplete type"/"undeclared identifier" errors on
    # /Yu-consumer files that a plain textual re-include of the header doesn't
    # produce). Dropping them just makes every file reparse stdafx.h from
    # scratch as an ordinary header, which is correct, just slower.
    return tok.startswith(("/Yc", "/Yu", "/Fp"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", help="compile_commands.json produced in the container")
    ap.add_argument("output", help="path to write the host-usable compile_commands.json")
    ap.add_argument("--container-root", required=True,
                    help=r"repo root as seen in the container, e.g. C:\Users\Docker\source\repos\xray-monolith")
    ap.add_argument("--host-root", required=True,
                    help="repo root on the host, e.g. /var/home/gooze/Development/xray-monolith")
    args = ap.parse_args()

    container_root = args.container_root.replace("\\", "/").rstrip("/")
    host_root = args.host_root.rstrip("/")

    def fix_path(p: str) -> str:
        p = p.replace("\\", "/")
        if p.lower().startswith(container_root.lower()):
            p = host_root + p[len(container_root):]
        return p

    with open(args.input) as f:
        data = json.load(f)

    extra_includes = repo_wide_includes(host_root)
    out = []
    for entry in data:
        directory = fix_path(entry["directory"])
        file_ = fix_path(entry["file"])
        command = entry["command"].replace("\\", "/")
        # after slash conversion, remap any embedded container-root occurrences
        # (compiler path, /Fo, /Fd, /I, etc.) - not just directory/file
        command = re.sub(re.escape(container_root), host_root, command, flags=re.IGNORECASE)

        tokens = split_command_line(command)
        # Force clang's cl.exe-compatible parsing mode explicitly. The captured
        # driver path's basename is "CL.exe" (as MSVC's toolset binary is
        # actually named on disk), which clang's argv[0]-based driver-mode
        # detection does not reliably match; "clang-cl" is unambiguous.
        tokens[0] = "clang-cl"
        own_name = file_.rsplit("/", 1)[-1]
        source_positions = [i for i, t in enumerate(tokens) if is_source_arg(t)]

        if len(source_positions) > 1:
            # batched invocation: keep only this entry's own source file
            filtered = []
            for i, t in enumerate(tokens):
                if i in source_positions and t.rsplit("/", 1)[-1].rsplit("\\", 1)[-1] != own_name:
                    continue
                filtered.append(t)
            tokens = filtered

        tokens = [t for t in tokens if not is_pch_arg(t)]
        tokens = [tokens[0]] + XWIN_SYSTEM_INCLUDES + extra_includes + tokens[1:]

        out.append({
            "directory": directory,
            "file": file_,
            "arguments": tokens,
        })

    with open(args.output, "w") as f:
        json.dump(out, f, indent=1)

    print(f"wrote {len(out)} entries to {args.output}")


if __name__ == "__main__":
    main()
