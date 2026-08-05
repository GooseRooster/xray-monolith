# File-Based Workflow for Gamedata Porting

## Overview

This document describes an optimized file-based approach for porting upstream gamedata changes to the Old World private repository. This approach is more efficient than commit-by-commit processing when dealing with large numbers of commits that touch the same files.

## When to Use File-Based Approach

**Use file-based approach when:**
- Many commits touch the same files (e.g., 20+ commits touching `lua_help_ex.script`)
- Files have complex evolution with additions, modifications, and deletions across multiple commits
- You want to see the complete picture of changes to a file

**Use commit-by-commit approach when:**
- Few commits with simple, isolated changes
- Need to understand the evolution history of a file
- Changes are small and independent

## File-Based Workflow

### Step 1: Identify Unique Files

Get all unique files from pending commits, excluding out-of-scope files:

```bash
python3 .agents/skills/upstream-merge-gamedata/scripts/gamedata_helpers.py pending | \
  jq -r '.[] | .engine_files[]' | \
  grep -v 'rus/' | \  # Exclude Russian localization
  sort | uniq
```

### Step 2: Classify Each File

For each unique file, determine its classification:

```bash
# For each file
FILE="gamedata/scripts/lua_help_ex.script"

# Check if exists in private tree
if [ ! -f "/path/to/private/tree/$FILE" ]; then
  echo "CLEAN_PORT: $FILE (does not exist in private tree)"
else
  # Get latest upstream version
  LATEST_COMMIT=$(git log --oneline --follow -- "$FILE" | head -1 | cut -d' ' -f1)
  git show "$LATEST_COMMIT:$FILE" > /tmp/upstream_$FILE
  
  # Compare with private tree
  diff -u "/path/to/private/tree/$FILE" /tmp/upstream_$FILE
  
  # Classify based on diff
  if [ $? -eq 0 ]; then
    echo "PORTED: $FILE (already matches upstream)"
  else
    echo "NEEDS_ADAPTATION: $FILE (diverged from upstream)"
  fi
fi
```

### Step 3: Process by Classification

#### Clean Port Files
Files that don't exist in private tree (and are in scope):

```bash
# Create the file directly from upstream
LATEST_COMMIT=$(git log --oneline --follow -- "$FILE" | head -1 | cut -d' ' -f1)
git show "$LATEST_COMMIT:$FILE" > "/path/to/private/tree/$FILE"
```

#### Already Ported Files
Files that already match upstream - no action needed.

#### Needs Adaptation Files
Files that exist in both trees but have diverged:

1. **Analyze the diff**: Understand what upstream changed vs. Old World's customizations
2. **Categorize changes**:
   - New functions/callbacks → Add to private tree
   - Modified functions → Adapt carefully, preserving Old World improvements
   - Removed functions → Check if Old World needs them, remove if not
   - Documentation → Add/update
   - Configuration → Merge changes
3. **Create adapted version**: Manual merge preserving the best of both
4. **Verify**: Test the adapted file doesn't break existing functionality

### Step 4: Batch Processing

For efficiency, process files in batches by priority:

#### Priority 1: Core Systems
- `_g_patches.script`
- `callbacks_gameobject.script`
- `lua_help_ex.script`

#### Priority 2: Gameplay Systems
- `aaaa_script_fixes_mp.script`
- `item_weapon.script`
- `sim_squad_scripted.script`

#### Priority 3: UI/Configuration
- `ui_mm_modded_exes.xml`
- `options_modded_exes_*.script`
- `ltx_help_ex.script`

## Example: Processing a File

### Example: `lua_help_ex.script`

```bash
# Step 1: Get latest upstream version
LATEST_COMMIT=$(git log --oneline --follow -- gamedata/scripts/lua_help_ex.script | head -1 | cut -d' ' -f1)
git show "$LATEST_COMMIT:gamedata/scripts/lua_help_ex.script" > /tmp/upstream_lua_help_ex.script

# Step 2: Compare with private tree
diff -u /var/home/gooze/repos/oldworld/_GAME/gamedata/scripts/lua_help_ex.script /tmp/upstream_lua_help_ex.script > /tmp/lua_help_ex_diff.patch

# Step 3: Analyze the diff
# Most changes are likely documentation additions (new Lua export docs)
# These can be safely added to the private tree

# Step 4: Create adapted version
# Copy private tree version as base
cp /var/home/gooze/repos/oldworld/_GAME/gamedata/scripts/lua_help_ex.script /tmp/adapted_lua_help_ex.script

# Add missing documentation from upstream
# (Manual process - add new function documentation blocks)

# Step 5: Verify and write
# Review the adapted file, then write to private tree
cp /tmp/adapted_lua_help_ex.script /var/home/gooze/repos/oldworld/_GAME/gamedata/scripts/lua_help_ex.script
```

## Tools and Scripts

### File Classification Script

```python
#!/usr/bin/env python3
import subprocess
import os
from collections import defaultdict

def classify_file(file_path):
    private_path = f"/path/to/private/tree/{file_path}"
    
    # Check private tree
    if not os.path.exists(private_path):
        if 'rus/' in file_path:
            return 'not_applicable', 'Russian localization'
        else:
            return 'clean_port', 'New file'
    
    # Get latest upstream
    try:
        result = subprocess.run(['git', 'log', '--oneline', '--follow', '--', file_path], 
                               capture_output=True, text=True)
        if result.stdout.strip():
            latest_commit = result.stdout.strip().split('\n')[0].split()[0]
            result = subprocess.run(['git', 'show', f'{latest_commit}:{file_path}'], 
                                   capture_output=True, text=True)
            if result.returncode == 0:
                upstream_content = result.stdout
            else:
                return 'not_applicable', 'Deleted in upstream'
        else:
            return 'not_applicable', 'No upstream history'
    except:
        return 'needs_followup', 'Error getting upstream content'
    
    # Compare
    with open(private_path, 'r') as f:
        private_content = f.read()
    
    if private_content.strip() == upstream_content.strip():
        return 'ported', 'Already matches upstream'
    else:
        return 'needs_adaptation', 'Diverged from upstream'
```

## Best Practices

1. **Always preserve Old World improvements**: If the private tree has a better implementation (e.g., callstack commented out), keep it
2. **Document decisions**: Record why certain changes were or weren't ported
3. **Test incrementally**: After adapting core files, test to ensure no regressions
4. **Batch similar files**: Process all documentation files together, all config files together
5. **Prioritize by impact**: Core systems first, UI last

## Common Patterns

### Pattern 1: Documentation Additions
**Upstream**: Added new Lua export documentation
**Private**: Missing the documentation
**Action**: Add the documentation blocks to private tree

### Pattern 2: New Callbacks
**Upstream**: Added new callback functions
**Private**: Missing the callbacks
**Action**: Add the callback declarations and handler functions

### Pattern 3: Refactored Systems
**Upstream**: Removed old callbacks, added new ones
**Private**: Has old callbacks, missing new ones
**Action**: Remove deprecated callbacks, add new ones

### Pattern 4: Configuration Changes
**Upstream**: Added new configuration options
**Private**: Missing the options
**Action**: Merge the new options with existing Old World configurations

## Privacy Reminder

- Never write private tree paths to tracked files
- Never write private file content to tracked files
- Only record dispositions using engine-relative paths
- All file operations are writes only (no git commands in private tree)