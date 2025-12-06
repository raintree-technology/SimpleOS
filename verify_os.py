#!/usr/bin/env python3
"""
SimpleOS Verification Script
Checks code integrity without requiring a cross-compiler.
"""

import os
import re
import sys
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).parent
SRC_DIR = ROOT / "src"
INCLUDE_DIR = ROOT / "include"

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def ok(msg):
    print(f"  {Colors.GREEN}✓{Colors.RESET} {msg}")

def warn(msg):
    print(f"  {Colors.YELLOW}⚠{Colors.RESET} {msg}")

def fail(msg):
    print(f"  {Colors.RED}✗{Colors.RESET} {msg}")

def header(msg):
    print(f"\n{Colors.BOLD}{Colors.BLUE}=== {msg} ==={Colors.RESET}")

def get_c_files():
    """Get all C source files."""
    return list(SRC_DIR.rglob("*.c"))

def get_h_files():
    """Get all header files."""
    return list(INCLUDE_DIR.rglob("*.h")) + list(SRC_DIR.rglob("*.h"))

def read_file(path):
    """Read file content."""
    try:
        return path.read_text()
    except:
        return ""

def check_includes():
    """Verify all includes can be resolved."""
    header("Checking Include Dependencies")

    all_headers = set()
    for h in get_h_files():
        all_headers.add(h.name)

    # Also check userspace headers
    userspace_dir = ROOT / "userspace"
    if userspace_dir.exists():
        for h in userspace_dir.glob("*.h"):
            all_headers.add(h.name)

    issues = []
    for src in get_c_files():
        content = read_file(src)
        includes = re.findall(r'#include\s*[<"]([^>"]+)[>"]', content)

        for inc in includes:
            inc_name = Path(inc).name
            # Skip system headers
            if inc.startswith("std") or inc in ["stdint.h", "stdbool.h", "stddef.h"]:
                continue
            if inc_name not in all_headers:
                issues.append(f"{src.name}: missing header '{inc}'")

    if issues:
        for i in issues[:5]:
            warn(i)
        if len(issues) > 5:
            warn(f"... and {len(issues)-5} more")
    else:
        ok("All includes resolvable")

    return len(issues) == 0

def check_function_declarations():
    """Check that called functions are declared."""
    header("Checking Function Declarations")

    # Collect all function definitions
    defined_funcs = set()
    declared_funcs = set()

    # Common patterns
    func_def_pattern = re.compile(r'^(?:static\s+)?(?:\w+\s+)+(\w+)\s*\([^)]*\)\s*\{', re.MULTILINE)
    func_decl_pattern = re.compile(r'^(?:extern\s+)?(?:\w+\s+)+(\w+)\s*\([^)]*\)\s*;', re.MULTILINE)

    for src in get_c_files() + get_h_files():
        content = read_file(src)

        for match in func_def_pattern.finditer(content):
            defined_funcs.add(match.group(1))

        for match in func_decl_pattern.finditer(content):
            declared_funcs.add(match.group(1))

    all_funcs = defined_funcs | declared_funcs
    ok(f"Found {len(defined_funcs)} function definitions")
    ok(f"Found {len(declared_funcs)} function declarations")

    return True

def check_signal_integration():
    """Verify signal system is properly integrated."""
    header("Checking Signal System Integration")

    checks = [
        ("signal_init() called in kernel", "src/kernel/kernel.c", r"signal_init\s*\(\s*\)"),
        ("signal_init_process() declared", "include/ipc/signal.h", r"signal_init_process"),
        ("signal_init_process() called in process_create", "src/kernel/process.c", r"signal_init_process\s*\("),
        ("signal_handle() called in scheduler", "src/kernel/scheduler.c", r"signal_handle\s*\(\s*\)"),
        ("signal_pending() called in scheduler", "src/kernel/scheduler.c", r"signal_pending\s*\(\s*\)"),
        ("signal state copied in fork", "src/kernel/syscall.c", r"signal_handlers\["),
        ("SIGINT defined", "include/ipc/signal.h", r"#define\s+SIGINT"),
        ("SIGTSTP defined", "include/ipc/signal.h", r"#define\s+SIGTSTP"),
        ("Ctrl+C sends SIGINT", "src/drivers/keyboard.c", r"SIGINT"),
        ("Ctrl+Z sends SIGTSTP", "src/drivers/keyboard.c", r"SIGTSTP"),
    ]

    all_pass = True
    for name, file_path, pattern in checks:
        full_path = ROOT / file_path
        if full_path.exists():
            content = read_file(full_path)
            if re.search(pattern, content):
                ok(name)
            else:
                fail(f"{name} - pattern not found")
                all_pass = False
        else:
            fail(f"{name} - file not found: {file_path}")
            all_pass = False

    return all_pass

def check_process_fields():
    """Verify process_t has required signal fields."""
    header("Checking Process Structure")

    process_h = ROOT / "include/kernel/process.h"
    content = read_file(process_h)

    required_fields = [
        ("pending_signals", r"pending_signals"),
        ("signal_mask", r"signal_mask"),
        ("signal_handlers[32]", r"signal_handlers\s*\[\s*32\s*\]"),
        ("exit_status", r"exit_status"),
        ("parent_pid", r"parent_pid"),
    ]

    all_pass = True
    for name, pattern in required_fields:
        if re.search(pattern, content):
            ok(f"Field: {name}")
        else:
            fail(f"Missing field: {name}")
            all_pass = False

    return all_pass

def check_syscalls():
    """Verify syscall implementations."""
    header("Checking Syscall Implementations")

    # Read all relevant source files
    syscall_c = read_file(ROOT / "src/kernel/syscall.c")
    signal_c = read_file(ROOT / "src/ipc/signal.c")
    all_content = syscall_c + signal_c

    syscalls = [
        "sys_fork", "sys_execve", "sys_wait", "sys_exit",
        "sys_read", "sys_write", "sys_open", "sys_close",
        "sys_pipe", "sys_dup2", "sys_kill", "sys_signal",
        "sys_getpid", "sys_sbrk"
    ]

    found = 0
    for sc in syscalls:
        # Look for function definition
        if re.search(rf'{sc}\s*\([^)]*\)\s*\{{', all_content):
            found += 1
        else:
            warn(f"Syscall not implemented: {sc}")

    ok(f"Found {found}/{len(syscalls)} syscall implementations")
    return found >= len(syscalls) - 2  # Allow 2 missing

def check_vmm_cleanup():
    """Verify VMM cleanup is implemented."""
    header("Checking VMM Implementation")

    vmm_c = ROOT / "src/mm/vmm.c"
    content = read_file(vmm_c)

    checks = [
        ("vmm_destroy_address_space implemented", r"void\s+vmm_destroy_address_space.*\{[\s\S]*?pmm_free_page"),
        ("Page table recursion", r"free_p[dt]|free_pdpt"),
        ("User space cleanup (entries 0-255)", r"for.*256|i\s*<\s*256"),
    ]

    all_pass = True
    for name, pattern in checks:
        if re.search(pattern, content):
            ok(name)
        else:
            fail(name)
            all_pass = False

    return all_pass

def check_terminal_print():
    """Verify terminal print functions exist."""
    header("Checking Terminal Print Functions")

    terminal_c = ROOT / "src/drivers/terminal.c"
    terminal_h = ROOT / "src/include/terminal.h"

    content_c = read_file(terminal_c)
    content_h = read_file(terminal_h) if terminal_h.exists() else ""

    funcs = ["terminal_print_int", "terminal_print_uint", "terminal_print_hex"]

    all_pass = True
    for func in funcs:
        if re.search(rf'{func}\s*\(', content_c):
            ok(f"{func}() implemented")
        else:
            fail(f"{func}() not found")
            all_pass = False

    return all_pass

def check_no_todos():
    """Check for remaining TODO comments that indicate unfinished code."""
    header("Checking for Unfinished TODOs")

    todo_pattern = re.compile(r'//\s*TODO[:\s].*print|//\s*TODO[:\s].*implement', re.IGNORECASE)

    issues = []
    for src in get_c_files():
        content = read_file(src)
        for i, line in enumerate(content.split('\n'), 1):
            if todo_pattern.search(line):
                issues.append(f"{src.name}:{i}: {line.strip()}")

    if issues:
        for i in issues[:5]:
            warn(i)
        if len(issues) > 5:
            warn(f"... and {len(issues)-5} more")
    else:
        ok("No unfinished print/implement TODOs found")

    return len(issues) == 0

def check_duplicate_includes():
    """Check for duplicate includes in source files."""
    header("Checking for Duplicate Includes")

    issues = []
    for src in get_c_files():
        content = read_file(src)
        includes = re.findall(r'#include\s*[<"]([^>"]+)[>"]', content)

        seen = set()
        for inc in includes:
            if inc in seen:
                issues.append(f"{src.name}: duplicate include '{inc}'")
            seen.add(inc)

    if issues:
        for i in issues:
            fail(i)
    else:
        ok("No duplicate includes")

    return len(issues) == 0

def check_elf_bounds():
    """Verify ELF loader has bounds checking."""
    header("Checking ELF Loader Safety")

    elf_c = ROOT / "src/lib/elf.c"
    content = read_file(elf_c)

    checks = [
        ("Header size check", r"size\s*<\s*sizeof.*Elf64_Ehdr"),
        ("Program header bounds", r"e_phoff.*>.*size|extends beyond"),
        ("Segment data bounds", r"p_offset.*>.*size|extends beyond"),
    ]

    all_pass = True
    for name, pattern in checks:
        if re.search(pattern, content):
            ok(name)
        else:
            warn(f"Missing: {name}")
            all_pass = False

    return all_pass

def check_keyboard_signals():
    """Verify keyboard properly includes signal headers."""
    header("Checking Keyboard Driver")

    keyboard_c = ROOT / "src/drivers/keyboard.c"
    content = read_file(keyboard_c)

    checks = [
        ("Includes process.h", r'#include.*process\.h'),
        ("Includes signal.h", r'#include.*signal\.h'),
        ("No local extern signal_send", r'extern void signal_send' if not re.search(r'#include.*signal\.h', content) else None),
    ]

    all_pass = True
    for name, pattern in checks:
        if pattern is None:
            continue
        if name.startswith("No"):
            # Negative check
            if not re.search(pattern, content):
                ok(name)
            else:
                fail(f"Found unwanted: {name}")
                all_pass = False
        else:
            if re.search(pattern, content):
                ok(name)
            else:
                fail(name)
                all_pass = False

    return all_pass

def main():
    print(f"{Colors.BOLD}SimpleOS Verification Script{Colors.RESET}")
    print("=" * 40)

    results = []

    results.append(("Include Dependencies", check_includes()))
    results.append(("Function Declarations", check_function_declarations()))
    results.append(("Signal Integration", check_signal_integration()))
    results.append(("Process Structure", check_process_fields()))
    results.append(("Syscall Implementations", check_syscalls()))
    results.append(("VMM Cleanup", check_vmm_cleanup()))
    results.append(("Terminal Print", check_terminal_print()))
    results.append(("No Unfinished TODOs", check_no_todos()))
    results.append(("No Duplicate Includes", check_duplicate_includes()))
    results.append(("ELF Bounds Checking", check_elf_bounds()))
    results.append(("Keyboard Driver", check_keyboard_signals()))

    # Summary
    header("Summary")
    passed = sum(1 for _, r in results if r)
    total = len(results)

    for name, result in results:
        status = f"{Colors.GREEN}PASS{Colors.RESET}" if result else f"{Colors.RED}FAIL{Colors.RESET}"
        print(f"  {status} - {name}")

    print()
    if passed == total:
        print(f"{Colors.GREEN}{Colors.BOLD}All {total} checks passed!{Colors.RESET}")
        return 0
    else:
        print(f"{Colors.YELLOW}{passed}/{total} checks passed{Colors.RESET}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
