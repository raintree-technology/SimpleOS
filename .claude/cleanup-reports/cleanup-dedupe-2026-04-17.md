# Duplication Assessment — 2026-04-17

## Summary
- Total clones found (raw): 5
  - 3 dropped as noise (generated `*_binary.h` ELF blobs in `userspace/`)
  - 2 real source-code clones
- HIGH confidence (auto-extractable): 0
- MEDIUM (similar but below HIGH thresholds or divergent): 2
- LOW: 0
- Estimated LOC saved (HIGH only): 0

jscpd run: `--min-tokens 50 --min-lines 15`, excluding `*_binary.h`, `node_modules/`, `.next/`, `out/`, `build/`, `grub-bios/`, `v86/`.

## Noise filtered (pre-report)
- `userspace/wc_binary.h`, `hello_binary.h`, `grep_binary.h` — generated ELF byte arrays embedded into the kernel build (produced by objcopy/xxd in the Makefile). The "duplication" is just the zero-padding in ELF section headers. Never DRY these; they are build artifacts.

## Clones

### Clone 1 — MEDIUM (shell wc builtin vs standalone wc)
- `userspace/shell.c:403-420` (18 LOC, 218 tokens) — `wc` branch of the shell's builtin command dispatcher, reads from `sys_read`, writes via `WRITE_STR`/`int_to_str`.
- `userspace/wc.c:9-32` — `wc_main()` in the standalone ELF userspace program.
- Divergence: the shell builtin lives inside an `else if (str_cmp(argv[0], "wc") == 0) { ... }` block with shared `argv`/`argc` context; the standalone is a top-level function with its own entry stub. Buffer names differ (`buf` vs `num_buf`).
- **Why not auto-extract:** below HIGH threshold (18 < 30 LOC). More importantly, the shell builtin and the ELF program target different build units — there is no shared userspace lib these can both link against without changes to `Makefile` and the ELF linking strategy. `userspace/ulib.h` is header-only macros, not a linkable lib. Proper fix is architectural: decide whether shell builtins for `wc`/`grep` are still needed now that standalone ELFs exist (see cleanup-legacy).

### Clone 2 — MEDIUM (shell grep builtin vs standalone grep)
- `userspace/shell.c:378-399` (22 LOC, 190 tokens) — `grep` branch of the shell's builtin dispatcher.
- `userspace/grep.c:35-58` — `grep_main()` in the standalone ELF.
- Divergence: shell uses `sys_read` with a buffer, standalone uses a `read_char()` helper loop. Same pattern-match logic (`str_str`) and same output format.
- **Why not auto-extract:** same reason as Clone 1. Also 22 < 30 LOC. Architectural question first, refactor second.

## Critical Assessment

The two real duplicates both reflect the same underlying design question: **the shell's `wc`/`grep` builtins duplicate the standalone userspace ELFs for the same programs.** In a typical Unix-like OS, the shell would not carry builtin implementations of these commands — it would exec the ELF from disk. These builtins probably predate the userspace ELF loader and stayed around as fallbacks.

This is a cleanup-legacy finding more than a cleanup-dedupe finding. The right answer is likely "delete the shell builtins for `wc` and `grep`, let the shell exec `/bin/wc` and `/bin/grep`" — but that requires verifying the exec path works for these programs first, which is out of scope for an automated dedupe pass.

No shared userspace library exists that could host extracted helpers. Creating one would be a new package — the skill explicitly forbids auto-creating packages.

Aside from these two, the kernel source (`src/`) is remarkably clone-free (0 duplicates across 24 C files / 5,447 LOC and 19 headers / 875 LOC). That's a good sign for kernel code quality.

## Actions taken
None. Both clones are MEDIUM; no auto-extraction.

## Verify
Skipped — no code changes to verify.

## Commit
No commit (no changes applied).

## Deferred for human review
- Clone 1 and Clone 2 above — recommend addressing via cleanup-legacy (remove shell builtins) rather than dedupe (extract shared helper).
