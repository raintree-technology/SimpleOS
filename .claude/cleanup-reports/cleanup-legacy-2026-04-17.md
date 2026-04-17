# Legacy Code Assessment — 2026-04-17

## Summary
- Deprecated symbols found: 0
- Legacy directories: 0
- Files matching `*_old.*`/`*-legacy.*`/`*_v1.*`: 0
- Dead fallback branches (`#if 0` / `if (false)`): 0
- TODOs / FIXMEs referring to removal: 0
- HIGH (auto-delete): 0
- MEDIUM (migration-planning required): 1
- LOW: 2

## Scan coverage
- Markers: `@deprecated`, `deprecated`, `legacy`, `obsolete`, `superseded`, `TODO.*remove`, `FIXME.*legacy`, `XXX.*remove`, `HACK`, `TMP`, `temporary`, `workaround`, `remove this`, `delete this`, `kill this` (case-insensitive).
- File types: `.c`, `.h`, `.ts`, `.tsx`, `.py`, `.sh`, `.md`.
- Preprocessor guards: `#if 0`, `#if false`, `if (0)`, `if (false)`.
- Directory patterns: `legacy/`, `old/`, `v1/`, `_archive/`, `deprecated/`.
- Filename patterns: `*_old.*`, `*-legacy.*`, `*.deprecated.*`, `*_v1.*`.
- Excluded: `node_modules/`, `build/`, `.next/`, `out/`, `grub-bios/` (third-party), `v86/` (third-party), `.git/`.

## Findings

### MEDIUM — shell builtin `wc`/`grep` duplicate standalone ELFs
- `userspace/shell.c:368-400` (grep builtin) and `userspace/shell.c:402-426` (wc builtin).
- Standalone equivalents: `userspace/grep.c`, `userspace/wc.c`, embedded as ELF blobs in `userspace/grep_binary.h`, `userspace/wc_binary.h`.
- The shell also supports `fork + execve` for external commands at `userspace/shell.c:479-492`. In principle, `wc` and `grep` could drop the builtin and be exec'd from `/bin/wc`, `/bin/grep`.
- Not marked `@deprecated`. The builtins are live code — they execute when the user types `wc` or `grep`, because the `if/else if` dispatcher hits the builtin branch before reaching the exec fallback.
- **Why not HIGH / not auto-delete:** this is a design decision, not a dead branch. Removing the builtins would route the commands through the exec path, which changes observable behavior (different error paths, process lifecycle, possibly different perf). That belongs to a human who can verify: `/bin/wc` and `/bin/grep` are actually installed in the running FS; the fork+exec path works end-to-end; no other shell features (piping, redirection) depend on the builtin implementations.
- Recommendation: bundle with the next userspace-architecture pass. Low-risk refactor once the exec path is validated.

### LOW — "unused" TSS fields
- `include/arch/i386/tss.h:13-16` — `esp1`/`ss1`/`esp2`/`ss2` fields commented "unused".
- These are hardware-layout fields in the x86 TSS structure. The OS only uses rings 0 and 3 so the ring-1/ring-2 stack pointers are unused *by software*, but the memory layout is mandated by the CPU. Cannot be removed — not legacy, just structural.

### LOW — `(void)regs;` suppressions
- `src/drivers/keyboard.c:92`, `src/drivers/timer.c:62` — standard C idiom to silence `-Wunused-parameter` on ISR handlers whose signature is fixed by the IDT callback convention.
- Not legacy code — intentional warning suppressions with the parameter retained for signature conformance.

## Noise dropped from findings
- `Makefile:117: "using i386-pc modules for legacy BIOS boot"` — "legacy" is a technical term here (legacy BIOS vs UEFI), not a deprecation marker.
- `grub-bios/lib/grub/i386-pc/config.h:9: GCRYPT_NO_DEPRECATED` — third-party GRUB tooling, out of scope.
- `src/kernel/syscall.c:695`, `userspace/ulib.h:155` — variables named `temp` in loops, not `TEMP:` markers.
- `verify_os.py:246-265` — a CI function that *checks* for TODOs in source; not itself a TODO.

## Critical Assessment

The codebase has effectively **zero traditional legacy debt**. No `@deprecated` markers, no `legacy/` directories, no `_old.*` files, no `#if 0` blocks, no TODO/FIXME comments slated for removal, no feature flags with stale branches. This is unusual and reflects a small, young codebase where the original authors haven't yet needed to deprecate anything — rather than a codebase that has been actively pruned.

The one real architectural-legacy candidate is the shell's builtin implementations of `wc` and `grep` — likely predating the working exec+fork path and retained as a fallback that no longer needs to exist. But this requires a human to verify the runtime filesystem actually ships `/bin/wc` and `/bin/grep`, and to sign off on the behavior change. Not automatable.

Nothing here warrants the cleanup-legacy auto-delete machinery.

## Actions taken
None.

## Verify
Skipped — no code changes to verify.

## Commit
No commit (no changes applied).

## Deferred for human review
- Shell `wc`/`grep` builtin removal (see MEDIUM finding) — low-risk once exec path is verified end-to-end.
