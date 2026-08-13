#!/usr/bin/env python3
"""Case-correct #include directives to match on-disk casing (Linux build).

The engine was developed on NTFS (case-insensitive), so a large number of
`#include "WrongCase.h"` directives resolve fine under MSVC but fail under
clang on a case-sensitive filesystem. This script finds and fixes them.

Fork policy (see docs/linux-cross-compile.md): fix include *text*, never the
on-disk filename. Renaming files would create a recurring merge tax on every
upstream pull; fixing the include text is a one-time, mergeable patch.

Safety rules:
  - PCH includes (`stdafx.h` / `StdAfx.h`) are SKIPPED - the "correct" casing
    is ambiguous across projects (each project has its own PCH) and must be
    resolved per translation unit against the compile DB, not by filesystem
    globbing.
  - A fix is applied only when resolution is unambiguous (every candidate root
    agrees on the same on-disk casing).

Usage:
  python3 tools/fix-include-case.py                 # dry run (report only)
  python3 tools/fix-include-case.py --apply         # write the fixes
  python3 tools/fix-include-case.py --root src/Layers   # restrict to a subtree
"""
import argparse
import collections
import os
import re

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, 'src')
XWIN_DEFAULT = os.path.join(os.path.expanduser('~'), '.xwin-cache', 'splat')

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
PCH_RE = re.compile(r'^(stdafx|StdAfx)\.h$')


def build_roots(root, xwin):
    roots = []
    for r in ['src/3rd party', 'src/3rd party/icu/include', 'sdk/include',
              'sdk/include/OpenAutomate', 'sdk/include/nvapi', 'sdk/include/dxsdk',
              'sdk/include/DPlay']:
        roots.append(os.path.join(root, r))
    for d in sorted(os.listdir(os.path.join(root, 'src'))):
        p = os.path.join(root, 'src', d)
        if os.path.isdir(p):
            roots.append(p)
    for r in ['crt/include', 'sdk/include/ucrt', 'sdk/include/um', 'sdk/include/shared']:
        roots.append(os.path.join(xwin, r))
    return roots


def resolve_from_base(base, rel):
    """Resolve rel relative to base, case-insensitively, preferring exact match
    per path component. Returns on-disk-cased rel, or None if not a file."""
    comps = [c for c in rel.replace('\\', '/').split('/') if c not in ('', '.')]
    cur = base
    resolved = []
    for comp in comps:
        if comp == '..':
            cur = os.path.dirname(cur)
            resolved.append('..')
            continue
        try:
            entries = os.listdir(cur)
        except OSError:
            return None
        if comp in entries:
            resolved.append(comp)
            cur = os.path.join(cur, comp)
            continue
        match = next((x for x in entries if x.lower() == comp.lower()), None)
        if match is None:
            return None
        resolved.append(match)
        cur = os.path.join(cur, match)
    if os.path.isfile(cur):
        return '/'.join(resolved)
    return None


def resolve(inc_dir, rel, quoted, roots):
    norm = rel.replace('\\', '/')
    if PCH_RE.match(norm.rsplit('/', 1)[-1]):
        return None
    cands = set()
    if quoted:
        r = resolve_from_base(inc_dir, norm)
        if r is not None:
            cands.add(r)
    for root in roots:
        r = resolve_from_base(root, norm)
        if r is not None:
            cands.add(r)
    return cands.pop() if len(cands) == 1 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--root', default=SRC, help='tree to scan (default: src/)')
    ap.add_argument('--xwin-root', default=XWIN_DEFAULT)
    args = ap.parse_args()

    roots = build_roots(REPO_ROOT, args.xwin_root)
    changes = []

    for dirpath, dirs, files in os.walk(args.root):
        dirs[:] = [d for d in dirs if d not in ('.git', 'x64', '.vs')]
        for fn in files:
            if not fn.lower().endswith(('.h', '.hpp', '.c', '.cc', '.cpp', '.cxx', '.inl')):
                continue
            p = os.path.join(dirpath, fn)
            try:
                with open(p, 'r', encoding='utf-8', errors='replace') as f:
                    text = f.read()
            except OSError:
                continue
            lines = text.splitlines(keepends=True)
            out = []
            file_changed = False
            for line in lines:
                m = INCLUDE_RE.match(line)
                if m:
                    kind, target = m.group(1), m.group(2)
                    fixed = resolve(dirpath, target, kind == '"', roots)
                    if fixed is not None and fixed != target.replace('\\', '/'):
                        changes.append((p, target.replace('\\', '/'), fixed))
                        line = line.replace(target, fixed, 1)
                        file_changed = True
                out.append(line)
            if args.apply and file_changed:
                with open(p, 'w', encoding='utf-8') as f:
                    f.writelines(out)

    c = collections.Counter((a, b) for _, a, b in changes)
    print(f'total include-case mismatches: {len(changes)} '
          f'(in {len(set(x[0] for x in changes))} files)')
    for (a, b), n in c.most_common(100):
        print(f'  {a:44s} -> {b:44s}  x{n}')
    if not args.apply:
        print('\n(dry run; pass --apply to write)')


if __name__ == '__main__':
    main()
