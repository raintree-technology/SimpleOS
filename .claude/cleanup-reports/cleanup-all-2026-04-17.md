# Full Cleanup — 2026-04-17

Pipeline scope: language-agnostic subset only (cleanup-dedupe, cleanup-legacy, cleanup-slop).
Rationale: repo is ~80% C kernel code (51 files / 10,577 LOC); most cleanup skills target JS/TS/Py/Go/Rust and would no-op. These 3 tools work across all languages in the repo.

Skipped skills: cleanup-unused, cleanup-cycles, cleanup-types, cleanup-weak-types, cleanup-defensive — not well-suited to C kernel code.

## Baseline
- Starting commit: `c2108584b0b6cc789dacec9bd209a619581bcc46`
- C/H/S source: 51 files, 10,577 LOC
- TS/TSX source (web/): 7 files, 1,560 LOC
- Python source: 1 file, 384 LOC

## Pipeline Run

| # | Skill | Status | Items | LOC delta | Commit | Report |
|---|-------|--------|-------|-----------|--------|--------|
| 3 | cleanup-dedupe  | ✓ no-op | 0 HIGH, 2 MEDIUM deferred | 0 | (no commit) | [link](cleanup-dedupe-2026-04-17.md) |
| 7 | cleanup-legacy  | ✓ no-op | 0 HIGH, 1 MEDIUM deferred | 0 | (no commit) | [link](cleanup-legacy-2026-04-17.md) |
| 8 | cleanup-slop    | ✓ applied | 11 comments removed (3 files) | -11 | `558cc34` | [link](cleanup-slop-2026-04-17.md) |

(Step numbers preserved from the full-pipeline spec; only 3/8/7 subset is running here.)

## After (initial pipeline — 3 skills)
- Post-pipeline commit: `558cc34`
- Total LOC delta: −11 (comments only)

## Follow-up (deferred items addressed)
After the pipeline, the two MEDIUM items from the reports were actioned under explicit user approval.

| Commit | Scope | LOC delta | Notes |
|--------|-------|-----------|-------|
| `60a8590` | Remove shell wc/grep builtins, route to `/bin/wc`, `/bin/grep` via fork+exec | −59 | Behavior change — relies on ramfs population at `src/fs/fs.c:224-236`. `shell_binary.h` stale until `make` re-runs. |
| `6ac631f` | Drop pure-restatement function-header comments across kernel, drivers, mm, ipc, fs, headers | −27 | Comments only. No code logic change. |
| `31816d3` | Second-pass inline cleanup in `vmm.c` + `grep.c` | −11 | Comments only. |

### Additional scans
- **knip on web/**: 3 flags, all false positives — `public/v86/libv86.js` is loaded at runtime via `<script src>`, `postcss-load-config` appears only in a JSDoc `@type` import, `lsof`/`make` are system binaries called from npm scripts. No unused exports or deps found.

## Final state
- Final commit: `31816d3`
- C/H/S source: 51 files (unchanged), 10,469 LOC (−108 from baseline)
- TS/TSX source (web/): unchanged
- Python source: unchanged
- Verify after each commit: `python3 verify_os.py` — 11/11 checks pass

## Cumulative delta from baseline
- Total LOC removed: **−108** (11 slop + 59 duplicate shell builtins + 27 function-header comments + 11 inline second-pass)
- Commits: 4
- Files touched: ~17 unique files (drivers, kernel, mm, ipc, fs, headers, userspace/shell.c, userspace/grep.c)
- Logic changes: 1 (shell builtin removal)
- Comment-only changes: 3 commits

## Halt Reason
None. All 3 recommended steps completed.

## Deferred Items
Aggregated MEDIUM findings across the 3 steps for human review:

1. **Shell builtin `wc`/`grep` duplicate standalone ELFs** — from cleanup-dedupe and cleanup-legacy.
   - `userspace/shell.c:368-400` (grep builtin) and `userspace/shell.c:402-426` (wc builtin).
   - Both duplicate the standalone `userspace/grep.c` and `userspace/wc.c` programs (embedded as ELF blobs via `*_binary.h`).
   - Shell already has a fork+exec path at `userspace/shell.c:479-492` that could handle these via `/bin/wc` and `/bin/grep`.
   - Action for human: validate the exec path works for these programs, then delete the two builtin branches. Architectural decision, not an automated refactor.

2. **Kernel C function-header comments** — from cleanup-slop.
   - Many kernel files have one-line `// Initialize X` comments above `void init_X(void)` function definitions.
   - Stylistic decision: keep as pseudo-docstrings (C convention) or delete in bulk (strict no-slop stance).
   - Not auto-applied; left for project-owner preference.

3. **Minor: duplicate file-header line in `userspace/grep.c:5`** — restates line 1 under a `Usage:` block. Very low-value cleanup.

## Skipped (not run)
cleanup-unused, cleanup-cycles, cleanup-types, cleanup-weak-types, cleanup-defensive — scoped out during preflight as poorly suited to a C-kernel-heavy repo (their tooling targets JS/TS/Py/Go/Rust; C has no try/catch or `any`-type equivalents).
