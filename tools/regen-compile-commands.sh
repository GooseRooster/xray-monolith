#!/usr/bin/env bash
# Regenerate the host-side compile_commands.json used by clangd from a fresh
# capture produced in the Windows build container (see tools/remap-compile-commands.py).
#
# Usage:
#   1. In the Windows container (Developer PowerShell for VS 2026), from the repo root:
#      msbuild src\engine-vs2022.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 `
#        "-logger:C:\path\to\MsBuildCompileCommandsJson\bin\Debug\netstandard2.0\CompileCommandsJson.dll;cctmp.json"
#      Move-Item cctmp.json <shared-folder>\compile_commands.json -Force
#   2. On the host: tools/regen-compile-commands.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONTAINER_JSON="${1:-/var/home/gooze/vms/windows/shared/compile_commands.json}"

python3 "$SCRIPT_DIR/remap-compile-commands.py" \
  "$CONTAINER_JSON" \
  "$REPO_ROOT/compile_commands.json" \
  --container-root 'C:\Users\Docker\source\repos\xray-monolith' \
  --host-root "$REPO_ROOT"
