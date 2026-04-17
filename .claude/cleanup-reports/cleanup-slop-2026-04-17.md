# Comment Slop Assessment — 2026-04-17

## Summary
- HIGH (deleted): 11 comment lines across 3 files
  - File-header AI platitudes: 6 lines (3 files × 2 lines)
  - Restated-code comments: 2 lines (`ports.c`)
  - Blank lines that accompanied the deleted headers: 3
- MEDIUM (reviewed, not deleted): ~55 function-header and section-marker comments across kernel — conventional C-style pseudo-docs. Retained as per "preserve when in doubt" rule.
- PRESERVED: all inline comments adding domain context (e.g., `// Initialize TSS BEFORE GDT (GDT setup loads TSS register)` — real invariant), all `(void)regs;` suppression comments, all TSS hardware-layout comments, all useful section markers inside long functions.

## Findings

### HIGH — `src/drivers/ports.c:1-2` + `:6` + `:13` (deleted)
File-header AI platitude + two comments that verbatim restate the function they sit above.
```c
// This file implements low-level I/O port operations for SimpleOS.
// It provides functions to read from and write to hardware ports using inline assembly.

// read a byte from a port
uint8_t inb(uint16_t port) { ... }

// send a byte to a port
void outb(uint16_t port, uint8_t data) { ... }
```
`inb`/`outb` are standard x86 port I/O primitives — the function name, signature, and inline asm together make the purpose unmistakable. The file header tells the reader exactly what the filename already says.

### HIGH — `src/drivers/keyboard.c:1-2` (deleted)
```c
// This file implements keyboard input handling for SimpleOS.
// It sets up the keyboard interrupt handler and translates scancodes to ASCII characters.
```
Matches the skill's canonical AI-platitude pattern (`// This X does Y`). The filename `keyboard.c` and the `#include` block below (ports.h, isr.h, signal.h) already convey everything this comment adds.

### HIGH — `src/drivers/terminal.c:1-2` (deleted)
```c
// This file implements terminal output functions for SimpleOS.
// It provides low-level screen manipulation for text mode display.
```
Same pattern. The `VGA_WIDTH`/`VGA_HEIGHT`/`VGA_MEMORY` constants immediately below make "text mode display" obvious.

### MEDIUM (retained) — function-header comments like `// Initialize scheduler` above `void scheduler_init(void)`
Found in: `src/kernel/scheduler.c`, `src/kernel/kernel.c`, `src/kernel/process.c`, `src/kernel/syscall.c`, `src/fs/fs.c`, `src/ipc/signal.c`, `src/ipc/pipe.c`, `src/mm/kmalloc.c`, `src/mm/pmm.c`, `src/mm/vmm.c`, `src/lib/elf.c`, `src/arch/i386/tss.c`, `src/boot/exceptions.c`, `src/drivers/vt.c`, `src/drivers/keyboard.c`, `src/drivers/timer.c`, `src/drivers/terminal.c`, and the matching header files.

These comments are duplicative of the function names in isolation, but in a C codebase without docstrings they function as navigation anchors — ctags/grep targets when jumping by function intent rather than name. Deleting them in bulk would measurably reduce readability for anyone skimming a long kernel source file. Leaving them for a human stylistic decision.

### MEDIUM (retained) — inline step-labels in long functions
Examples: `// Check magic`, `// Check 32-bit`, `// Check executable`, `// Check i386` inside `elf_validate()` at `src/lib/elf.c:20-42`; `// Print panic header`, `// Print interrupt info`, `// Print general purpose registers` inside `panic_with_regs()` at `src/kernel/panic.c:49-70`.

These chunk long procedures into named phases. A reader glancing at the function can see the 4-step ELF validation or the panic-printer sections without parsing each line. Borderline redundant in isolation; useful in aggregate. Not deleted.

### MEDIUM (retained) — `// Flush TLB` comments
`src/mm/vmm.c:150`, `src/mm/vmm.c:375`. These sit above `asm volatile("invlpg ...")`. The asm opcode is obscure to anyone who doesn't have x86 memory-management mnemonics memorized; the comment is the mapping from instruction to effect. Kept.

### PRESERVED (exemplary) — `src/kernel/kernel.c:421`
```c
// Initialize TSS BEFORE GDT (GDT setup loads TSS register)
```
Invariant with consequence. Perfect comment. Keep.

### PRESERVED — `(void)regs;  // Unused parameter` and similar
`src/drivers/keyboard.c:92`, `src/drivers/timer.c:62`. Mandatory C idiom for suppressing `-Wunused-parameter` on ISR handlers whose signatures are fixed by the IDT ABI. Comment explains why the parameter is retained despite being unused.

### PRESERVED — TSS "unused" comments
`include/arch/i386/tss.h:13,16` — document that `esp1`/`esp2` are unused *by this OS* but required by the CPU's TSS layout. Non-obvious hardware constraint.

## Critical Assessment

The project's comment style is mostly healthy. The few clear slop lines (3 file headers + 2 restated-code comments) all share the signature of AI-assisted code generation — formulaic "This file implements X" preambles and one-line restatements of the next function. Their presence in three different driver files with the same pattern suggests a single generation pass; the rest of the kernel doesn't exhibit this pattern, which is why the kernel code (`src/kernel/`, `src/mm/`, `src/fs/`) is cleaner.

The kernel also shows examples of excellent commenting: the TSS-before-GDT ordering constraint, the CPU-layout explanation for unused TSS fields, the `(void)regs` idiom documentation. These are exactly the "tells a future reader something they couldn't get from the code" comments the skill is designed to preserve.

Recommend the 11-line cleanup as auto-applied. The remaining ~55 function-header/section-marker comments are a matter of stylistic preference and belong to a human decision, not an automated pass — in a C codebase without docstrings, removing all of them would cost more readability than it saves.

## Actions taken
- Deleted 11 lines across 3 files in a single commit.
- Verified with `python3 verify_os.py` (11/11 structural checks passed).

## Verify
`python3 verify_os.py` → PASS (11/11 checks).
No other verify tooling runs at the repo root. Web/ untouched, so no biome/tsc run needed.

## Commit
`558cc34` — "chore(cleanup): cleanup-slop — removed 11 unhelpful comments"

## Deferred for human review
- Function-header and section-marker comments listed under MEDIUM. Stylistic decision on whether to keep the C convention of pseudo-docstring comments above every function.
- `userspace/grep.c:5` — minor: line 5 "Reads from stdin, prints lines containing <pattern>" restates the file-header line 1. Lives under a `Usage:` block so is mildly justified; low-value cleanup.
