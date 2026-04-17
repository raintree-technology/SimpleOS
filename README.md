# SimpleOS

> An operating system built from scratch in C and x86 assembly that runs in your browser.
>
> [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
> [![Build](https://img.shields.io/github/actions/workflow/status/raintree-technology/SimpleOS/ci.yml?branch=main)](https://github.com/raintree-technology/SimpleOS/actions)
> [![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)
> [![Contributors](https://img.shields.io/github/contributors/raintree-technology/SimpleOS)](https://github.com/raintree-technology/SimpleOS/graphs/contributors)
>
> SimpleOS is a learning project — a from-scratch kernel that implements processes, virtual memory, syscalls, pipes, signals, a filesystem, and a shell, all in roughly 4,500 lines of code. It boots as a real ISO image via GRUB, and a bundled [web app](web/) lets you run the entire OS in-browser using the [v86](https://github.com/nickvdp/nickvdp.github.io) x86 emulator. No toolchain, no VM, just open a page and you're inside a working OS.
>
> This isn't a production operating system. It's a proof of concept and a learning tool — built to understand how operating systems actually work by writing one from the ground up.
>
> ---
>
> ## Table of Contents
>
> - [Demo](#demo)
> - - [Features](#features)
>   - - [What's Inside](#whats-inside)
>     - - [Memory Layout](#memory-layout)
>       - - [Syscalls](#syscalls)
>         - - [Shell](#shell)
>           - - [Repository Layout](#repository-layout)
>             - - [Getting Started](#getting-started)
>               - - [Contributing](#contributing)
>                 - - [License](#license)
>                  
>                   - ---
>
> ## Demo
>
> Open the live demo in your browser — no install required:
>
> > **[Try SimpleOS in your browser →](https://raintree-technology.github.io/SimpleOS)**
> >
> > The web app boots the ISO in a v86 emulator with a CRT-styled terminal UI.
> >
> > ---
> >
> > ## Features
> >
> > - Preemptive round-robin scheduler with 10-tick quantum
> > - - Two-level paging with copy-on-write `fork()`
> >   - - 18 syscalls via `int 0x80` (fork, execve, wait, pipe, dup2, kill, and more)
> >     - - Unix-style signals (SIGINT, SIGKILL, SIGSTOP, etc.)
> >       - - In-memory ramfs with pre-populated `/bin/*`
> >         - - VGA text mode, PS/2 keyboard, PIT timer, virtual terminal switching (Alt+F1–F4)
> >           - - 32-bit ELF loader for user-mode binaries
> >             - - Freestanding userspace: shell, grep, wc, hello
> >               - - Runs entirely in-browser via the v86 x86 emulator
> >                
> >                 - ---
> >
> > ## What's Inside
> >
> > | Subsystem | Key files | What it does |
> > |---|---|---|
> > | **Boot** | [boot.s](src/arch/i386/boot.s), [grub.cfg](boot/grub/grub.cfg), [linker.ld](linker.ld) | Multiboot2 entry, GDT, stack setup, jumps to `kernel_main` |
> > | **Kernel core** | [kernel.c](src/kernel/kernel.c) | Initializes every subsystem, creates test processes, starts scheduler |
> > | **Processes** | [process.c](src/kernel/process.c), [scheduler.c](src/kernel/scheduler.c) | Round-robin preemptive scheduling (10-tick quantum), kernel threads and user processes, up to 64 |
> > | **Context switching** | [context_switch.s](src/arch/i386/context_switch.s) | Saves/restores register state, interrupt-return trampoline for user mode |
> > | **Memory** | [pmm.c](src/mm/pmm.c), [vmm.c](src/mm/vmm.c), [kmalloc.c](src/mm/kmalloc.c) | Bitmap physical allocator, two-level paging with copy-on-write, bump-pointer heap |
> > | **Syscalls** | [syscall.c](src/kernel/syscall.c) | 18 syscalls via `int 0x80` — fork, execve, wait, pipe, dup2, kill, and more |
> > | **IPC** | [pipe.c](src/ipc/pipe.c), [signal.c](src/ipc/signal.c) | 512-byte circular pipes, Unix-style signals (SIGINT, SIGKILL, SIGSTOP, etc.) |
> > | **Filesystem** | [fs.c](src/fs/fs.c) | In-memory ramfs — 64 nodes, 512KB of 512-byte blocks, pre-populated with `/bin/*` |
> > | **Drivers** | [terminal.c](src/drivers/terminal.c), [keyboard.c](src/drivers/keyboard.c), [timer.c](src/drivers/timer.c), [vt.c](src/drivers/vt.c) | VGA text mode, PS/2 keyboard with Ctrl+C/Z, PIT timer, virtual terminal switching (Alt+F1–F4) |
> > | **ELF loader** | [elf.c](src/lib/elf.c) | Loads 32-bit ELF binaries into user address space |
> > | **User mode** | [usermode.c](src/arch/i386/usermode.c), [tss.c](src/arch/i386/tss.c) | Ring 0 → Ring 3 via IRET, TSS for kernel stack on interrupts |
> > | **Userspace** | [shell.c](userspace/shell.c), [grep.c](userspace/grep.c), [wc.c](userspace/wc.c), [hello.c](userspace/hello.c) | Freestanding programs built as ELF, converted to C byte arrays, embedded in the kernel image |
> > | **Web demo** | [Emulator.tsx](web/Emulator.tsx), [CRTFrame.tsx](web/CRTFrame.tsx) | v86 emulator with asset preflight, CRT-styled terminal with animated ASCII background |
> >
> > ### Boot Flow
> >
> > ```
> > GRUB (Multiboot2)
> >   → _start            src/arch/i386/boot.s       load GDT, set up stack
> >   → kernel_main       src/kernel/kernel.c         init all subsystems
> >   → scheduler_enable  src/kernel/scheduler.c      start round-robin loop
> >   → idle process      PID 0 runs when nothing else is ready
> > ```
> >
> > The kernel is linked at 1MB (`linker.ld`). The heap lives at 2–3.5MB. User processes get virtual addresses starting at 128MB, with stacks below 3GB.
> >
> > ---
> >
> > ## Memory Layout
> >
> > | Address | Region |
> > |---|---|
> > | `0x00100000` (1MB) | Kernel code (`.multiboot`, `.text`, `.data`, `.bss`) |
> > | `0x00200000` (2MB) | Kernel heap start |
> > | `0x00380000` (3.5MB) | Kernel heap end |
> > | `0x00400000` (4MB) | Physical memory allocator starts here |
> > | `0x04000000` (64MB) | End of kernel identity-mapped window |
> > | `0x08000000` (128MB) | User virtual address space begins |
> > | `0xBFFFF000` (~3GB) | User stack top (grows down) |
> > | `0xC0000000` (3GB) | Kernel boundary |
> >
> > ---
> >
> > ## Syscalls
> >
> > All syscalls use `int 0x80` with arguments in `eax` (number), `ebx`, `ecx`, `edx`. Userspace wrappers live in `ulib.h`.
> >
> > | # | Name | Description |
> > |---|---|---|
> > | 1 | `exit` | Terminate process |
> > | 2 | `write` | Write to fd (terminal, file, or pipe) |
> > | 3 | `read` | Read from fd |
> > | 4 | `getpid` | Get current PID |
> > | 5 | `sleep` | Sleep N milliseconds |
> > | 6 | `sbrk` | Grow user heap |
> > | 7 | `fork` | Fork process (COW page tables) |
> > | 8 | `wait` | Wait for child, reap zombie |
> > | 9 | `execve` | Load and run ELF binary |
> > | 10 | `ps` | List all processes |
> > | 11 | `open` | Open file |
> > | 12 | `close` | Close fd |
> > | 13 | `stat` | File metadata |
> > | 14 | `mkdir` | Create directory |
> > | 15 | `readdir` | Read directory entries |
> > | 16 | `kill` | Send signal to process |
> > | 17 | `pipe` | Create pipe pair |
> > | 18 | `dup2` | Duplicate fd |
> >
> > ---
> >
> > ## Shell
> >
> > The shell supports:
> >
> > - **Built-ins:** `help`, `ps`, `echo`, `cd`, `clear`, `exit`, `jobs`, `bg`, `fg`, `kill`
> > - - **External programs:** `/bin/hello`, `/bin/grep`, `/bin/wc` (via fork + execve)
> >   - - **Pipes:** `echo hello | wc`
> >     - - **Redirection:** `cmd > file`, `cmd < file`
> >       - - **Background jobs:** `cmd &`, then `jobs`, `fg`, `bg`
> >         - - **Job control:** Ctrl+C (SIGINT), Ctrl+Z (SIGTSTP)
> >           - - **History:** up/down arrows (10 entries)
> >            
> >             - ---
> >
> > ## Repository Layout
> >
> > ```
> > src/
> >   arch/i386/         boot.s, asm_functions.s, context_switch.s, tss.c, usermode.c
> >   boot/              exceptions.c (ISR/IDT setup)
> >   kernel/            kernel.c, process.c, scheduler.c, syscall.c, panic.c
> >   mm/                kmalloc.c, pmm.c, vmm.c
> >   drivers/           terminal.c, keyboard.c, timer.c, ports.c, vt.c
> >   fs/                fs.c
> >   ipc/               pipe.c, signal.c
> >   lib/               elf.c, string.c
> >   include/           headers mirroring src/ layout
> > userspace/           shell.c, grep.c, wc.c, hello.c, ulib.h, Makefile
> > boot/grub/           grub.cfg
> > grub-bios/           pre-built GRUB i386-pc modules for ISO creation
> > web/                 Next.js app (Emulator.tsx, CRTFrame.tsx, page.tsx)
> > scripts/             dev-shell.sh, setup-toolchain.sh
> > .devcontainer/       Dockerfile + devcontainer.json
> > ```
> >
> > ---
> >
> > ## Getting Started
> >
> > The kernel targets 32-bit freestanding x86. The Makefile cross-compiles with `i686-elf-gcc` and produces `simpleos.iso` via `grub-mkrescue`. Userspace programs are compiled separately (`userspace/Makefile`), then converted to C byte arrays with `xxd -i` and `#include`d into the kernel's filesystem init.
> >
> > ### Dev Container (recommended)
> >
> > Open the repo in VS Code, Cursor, or Zed — the devcontainer config has everything pre-installed.
> >
> > ```sh
> > make
> > cd web && npm install && npm run dev
> > ```
> >
> > ### Local Toolchain
> >
> > See [`scripts/setup-toolchain.sh`](scripts/setup-toolchain.sh) for install instructions. You need:
> >
> > - `i686-elf-gcc` / `i686-elf-as`
> > - - `x86_64-elf-grub-mkrescue` (for ISO creation)
> >   - - `qemu-system-i386` (for local testing)
> >    
> >     - ```sh
> >       make           # build simpleos.iso
> >       make run       # boot in QEMU
> >       ```
> >
> > ### Docker Build
> >
> > ```sh
> > ./build.sh     # builds simpleos.iso → web/public/os/
> > ```
> >
> > The Dockerfile extends a cross-compiler image with GRUB and xorriso. You can also get an interactive shell with `scripts/dev-shell.sh`.
> >
> > ### Browser Demo
> >
> > The web app expects three assets:
> >
> > - `web/public/os/simpleos.iso` — built by `make` or `./build.sh`
> > - - `web/public/bios/seabios.bin` — SeaBIOS ROM
> >   - - `web/public/bios/vgabios.bin` — VGA BIOS ROM
> >    
> >     - If anything is missing, the preflight check shows exactly what's absent instead of booting into a broken state.
> >    
> >     - ```sh
> >       cd web
> >       npm install
> >       npm run dev          # starts on port 3500
> >       npm run build:os     # rebuild kernel and copy ISO in one step
> >       ```
> >
> > ### Reading the Code
> >
> > Start here: [`src/kernel/kernel.c`](src/kernel/kernel.c) — `kernel_main()` initializes every subsystem in order and is the best map of how the pieces connect.
> >
> > From there:
> >
> > - **Boot:** `boot.s` → how the CPU gets from GRUB to `kernel_main`
> > - - **Interrupts:** `asm_functions.s` → ISR stubs, IDT/GDT loading, paging enable
> >   - - **Processes:** `process.c` → PCB structure, creation, destruction
> >     - - **Scheduling:** `scheduler.c` → preemptive round-robin, context switch calls
> >       - - **Memory:** `vmm.c` → page tables, COW fork, address space management
> >         - - **Syscalls:** `syscall.c` → `int 0x80` dispatch, all 18 implementations
> >           - - **Userspace:** `ulib.h` → syscall wrappers that user programs call
> >             - - **Shell:** `shell.c` → pipes, redirection, job control in ~500 lines
> >              
> >               - `verify_os.py` checks code integrity without compiling — include dependencies, function signatures, syscall coverage, signal integration, and common issues like duplicate includes or unfinished TODOs.
> >              
> >               - ---
> >
> > ## Contributing
> >
> > Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
> >
> > **Quick guide:**
> >
> > 1. Fork the repo and create a branch: `git checkout -b feat/your-feature`
> > 2. 2. Make your changes and ensure the build passes: `make`
> >    3. 3. Run the verifier: `python3 verify_os.py`
> >       4. 4. Open a pull request with a clear description of what changed and why
> >         
> >          5. Please follow the existing code style (K&R-adjacent C, descriptive names, comments on non-obvious logic). For larger changes, open an issue first to discuss the approach.
> >         
> >          6. To report a bug or request a feature, [open an issue](https://github.com/raintree-technology/SimpleOS/issues/new/choose).
> >         
> >          7. For security vulnerabilities, please see [SECURITY.md](SECURITY.md) and **do not** open a public issue.
> >
> > ---
> >
> > ## License
> >
> > [MIT](LICENSE) © raintree-technology
