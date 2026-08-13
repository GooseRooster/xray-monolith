#!/usr/bin/env python3
"""Run clang-cl -fsyntax-only over compile_commands.json and report per-project
clean counts + first-error categories.

This is the "how far along is the cross-compile" gauge. It compiles each
translation unit listed in the compilation database with `clang-cl -fsyntax-only`
and buckets every failing file by its first error. Run it after a batch of
source fixes to see the clean-rate delta.

The compile_commands.json entries carry the include dirs / defines captured from
a real MSBuild run (see tools/README.md), so this exercises the same per-file
flags a real clang-cl cross-compile would use.

Usage:
  python3 tools/sweep-clang.py --engine-only                 # skip 3rd-party + sdk
  python3 tools/sweep-clang.py --sample 40                   # quick smoke sweep
  python3 tools/sweep-clang.py --out results.json            # save per-file results

Notes:
  - Requires `clang-cl` on PATH and a generated compile_commands.json.
  - Exceptions are intentionally NOT enabled: the engine builds with
    /EHs-c- (xrCore.h #errors if _CPPUNWIND is defined), and clang-cl's
    default -fno-exceptions matches that.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import random
import re
import subprocess

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def detect_stale_root(data, root):
    """Find the on-disk prefix the DB was captured against (the repo root *as
    seen at capture time*), so we can re-anchor it at the current checkout.

    The DB's absolute paths are tied to wherever the repo lived when MSBuild
    produced the capture (a different host or a moved checkout). We locate that
    stale root by finding the repo basename in the first entry's directory and
    returning everything up to and including it.
    """
    base = os.path.basename(root)
    marker = '/' + base + '/'
    for e in data:
        d = e.get('directory', '')
        i = d.find(marker)
        if i != -1:
            return d[:i + len(marker)]  # includes trailing slash
    return None


def rebase(path, stale_root, root):
    """Re-anchor a captured path at the current repo root."""
    if not path:
        return path
    if stale_root is None or path.startswith(root.rstrip('/') + '/'):
        return path
    return path.replace(stale_root, root + '/')


def project_of(entry, root):
    rest = rebase(entry['directory'], None, root).split(os.path.basename(root), 1)[-1].lstrip('/')
    if rest.startswith('src/3rd party'):
        return 'src/3rd party'
    if rest.startswith('sdk/'):
        return 'sdk'
    parts = rest.split('/')
    return parts[1] if len(parts) > 1 else rest


def build_cmd(entry, stale_root, root, extra):
    args = [rebase(a, stale_root, root) for a in entry['arguments'][1:]]
    src = rebase(os.path.join(entry['directory'], entry['file']), stale_root, root)
    out = []
    for a in args:
        if a == src or a.endswith(os.path.basename(src)):
            out += ['-fsyntax-only', src]
        else:
            out.append(a)
    out = [a for a in out if a != '/c']
    out += extra + ['-Wno-microsoft-include', '-Wno-register', '-ferror-limit=6']
    return ['clang-cl'] + out


def categorize(errs):
    if not errs:
        return 'CLEAN'
    for line in errs:
        m = re.search(r'error: (.*)', line)
        if not m:
            continue
        msg = m.group(1)
        if 'template specialization requires' in msg:
            return 'template<> missing'
        if 'no member named' in msg:
            return 'dep-base no-member'
        if 'no template named' in msg:
            return 'no-template-named'
        if 'use of undeclared identifier' in msg:
            return 'undeclared-ident'
        if 'unknown type name' in msg:
            return 'unknown-type'
        if 'file not found' in msg:
            return 'file-not-found: ' + msg.split("'")[1]
        if 'no matching function' in msg:
            return 'no-match-fn'
        if 'redefinition' in msg:
            return 'redefinition'
        if 'register' in msg:
            return 'register'
        if 'expected' in msg and ';' in msg:
            return 'expected-syntax'
        return 'other: ' + msg[:45]
    return 'TIMEOUT/OTHER'


def run_one(entry, stale_root, root, extra):
    cwd = rebase(entry['directory'], stale_root, root)
    if not os.path.isdir(cwd):
        return ('STALE-DIR', [])
    src = rebase(os.path.join(entry['directory'], entry['file']), stale_root, root)
    if not os.path.isfile(src):
        return ('STALE-FILE', [])
    try:
        p = subprocess.run(build_cmd(entry, stale_root, root, extra), capture_output=True,
                           text=True, timeout=90, cwd=cwd)
    except subprocess.TimeoutExpired:
        return ('TIMEOUT', [])
    errs = [l for l in p.stderr.splitlines() if 'error:' in l]
    return (categorize(errs), errs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo-root', default=REPO_ROOT)
    ap.add_argument('--db', default=os.path.join(REPO_ROOT, 'compile_commands.json'))
    ap.add_argument('--engine-only', action='store_true',
                    help='exclude sdk/ and src/3rd party translation units')
    ap.add_argument('--sample', type=int, default=0,
                    help='cap the number of files sampled per project (0 = all)')
    ap.add_argument('--workers', type=int, default=8)
    ap.add_argument('--out', default=None, help='write per-file results JSON')
    ap.add_argument('--extra', nargs='*', default=[],
                    help='extra clang-cl flags, e.g. --extra /clang:-fno-operator-names')
    args = ap.parse_args()

    root = args.repo_root.rstrip('/')
    data = json.load(open(args.db))
    stale_root = detect_stale_root(data, root)
    entries = data
    if args.engine_only:
        entries = [e for e in data if project_of(e, root) not in ('sdk', 'src/3rd party')]
    if args.sample:
        random.seed(42)
        byproj = collections.defaultdict(list)
        for e in entries:
            byproj[project_of(e, root)].append(e)
        entries = []
        for _, es in sorted(byproj.items()):
            entries += random.sample(es, min(args.sample, len(es)))

    extra = args.extra + ['/clang:-fno-operator-names']

    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futures = {ex.submit(run_one, e, stale_root, root, extra): e for e in entries}
        for i, f in enumerate(concurrent.futures.as_completed(futures)):
            e = futures[f]
            cat, _ = f.result()
            results[os.path.join(e['directory'], e['file'])] = cat

    byproj = collections.defaultdict(collections.Counter)
    total = collections.Counter()
    for f, cat in results.items():
        p = project_of({'directory': os.path.dirname(f), 'file': os.path.basename(f)}, root)
        byproj[p][cat] += 1
        total[cat] += 1

    n = len(results)
    clean = total['CLEAN']
    live = n - total.get('STALE-FILE', 0) - total.get('STALE-DIR', 0)
    print(f'TOTAL {n}  CLEAN {clean} ({100.0 * clean / n:.1f}%)  '
          f'live={live} ({100.0 * clean / live:.1f}% clean)')
    print('\nFirst-error categories (all):')
    for cat, c in total.most_common(40):
        print(f'  {cat:45s} {c}')
    print('\nPer-project clean rate:')
    for p in sorted(byproj):
        c = byproj[p]
        tot = sum(c.values())
        cl = c['CLEAN']
        print(f'  {p:20s} {cl:4d}/{tot:4d} ({100.0 * cl / tot:5.1f}%)')

    if args.out:
        with open(args.out, 'w') as fh:
            json.dump({rebase(f, stale_root, root).replace(root + '/', ''): c for f, c in results.items()},
                      fh, indent=1)
        print(f'\nwrote {args.out}')


if __name__ == '__main__':
    main()
