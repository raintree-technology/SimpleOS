#include "lib/elf.h"
#include "kernel/process.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "drivers/terminal.h"

#ifdef DEBUG_ELF
#define ELF_DEBUG(...) terminal_writestring(__VA_ARGS__)
#define ELF_DEBUG_HEX(v) terminal_print_hex(v)
#define ELF_DEBUG_INT(v) terminal_print_int(v)
#else
#define ELF_DEBUG(...) ((void)0)
#define ELF_DEBUG_HEX(v) ((void)0)
#define ELF_DEBUG_INT(v) ((void)0)
#endif

// Validate ELF header (32-bit)
static int elf_validate(Elf32_Ehdr* header) {
    // Check magic
    if (header->e_ident[0] != ELFMAG0 ||
        header->e_ident[1] != ELFMAG1 ||
        header->e_ident[2] != ELFMAG2 ||
        header->e_ident[3] != ELFMAG3) {
        terminal_writestring("ELF: Invalid magic\n");
        return -1;
    }

    // Check 32-bit
    if (header->e_ident[4] != ELFCLASS32) {
        terminal_writestring("ELF: Not 32-bit\n");
        return -1;
    }

    // Check executable
    if (header->e_type != ET_EXEC) {
        terminal_writestring("ELF: Not executable\n");
        return -1;
    }

    // Check i386
    if (header->e_machine != EM_386) {
        terminal_writestring("ELF: Wrong architecture\n");
        return -1;
    }

    if (header->e_phentsize != sizeof(Elf32_Phdr)) {
        terminal_writestring("ELF: Unexpected program header size\n");
        return -1;
    }

    if (header->e_entry < USER_VADDR_MIN || header->e_entry >= KERNEL_BASE) {
        terminal_writestring("ELF: Entry point outside user range\n");
        return -1;
    }

    return 0;
}

// Load ELF segments into process address space (32-bit)
int elf_load(process_t* process, void* elf_data, size_t size) {
    // Bounds check: ensure we have at least an ELF header
    if (size < sizeof(Elf32_Ehdr)) {
        terminal_writestring("ELF: File too small for header\n");
        return -1;
    }

    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;

    // Validate
    if (elf_validate(header) < 0) {
        return -1;
    }

    // Bounds check: ensure program headers are within the file
    if (header->e_phoff > size) {
        terminal_writestring("ELF: Invalid program header offset\n");
        return -1;
    }

    if (header->e_phoff + ((size_t)header->e_phnum * sizeof(Elf32_Phdr)) > size) {
        terminal_writestring("ELF: Program headers extend beyond file\n");
        return -1;
    }

    ELF_DEBUG("ELF: Loading executable, entry=");
    ELF_DEBUG_HEX(header->e_entry);
    ELF_DEBUG("\n");

    // Get program headers
    Elf32_Phdr* phdrs = (Elf32_Phdr*)((uint8_t*)elf_data + header->e_phoff);

    // Process each segment
    uint32_t highest_loaded = USER_CODE_START;
    for (int i = 0; i < header->e_phnum; i++) {
        Elf32_Phdr* phdr = &phdrs[i];

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        ELF_DEBUG("ELF: Loading segment ");
        ELF_DEBUG_INT(i);
        ELF_DEBUG(" vaddr=");
        ELF_DEBUG_HEX(phdr->p_vaddr);
        ELF_DEBUG(" memsz=");
        ELF_DEBUG_HEX(phdr->p_memsz);
        ELF_DEBUG("\n");

        if (phdr->p_memsz < phdr->p_filesz) {
            terminal_writestring("ELF: memsz smaller than filesz\n");
            return -1;
        }

        if (phdr->p_memsz == 0) {
            continue;
        }

        uint32_t segment_end = phdr->p_vaddr + phdr->p_memsz;
        if (segment_end < phdr->p_vaddr) {
            terminal_writestring("ELF: Segment address overflow\n");
            return -1;
        }

        if (phdr->p_vaddr < USER_VADDR_MIN || segment_end >= KERNEL_BASE) {
            terminal_writestring("ELF: Segment outside user range\n");
            return -1;
        }

        // Calculate required pages
        uint32_t start = PAGE_ALIGN_DOWN(phdr->p_vaddr);
        uint32_t end = PAGE_ALIGN_UP(phdr->p_vaddr + phdr->p_memsz);
        uint32_t pages = (end - start) / PAGE_SIZE;

        // Map pages
        for (uint32_t j = 0; j < pages; j++) {
            uint32_t vaddr = start + j * PAGE_SIZE;
            void* phys_page = pmm_alloc_page();

            if (!phys_page) {
                terminal_writestring("ELF: Out of memory\n");
                return -1;
            }

            uint32_t paddr = (uint32_t)phys_page;

            // Set permissions
            uint32_t flags = PAGE_PRESENT | PAGE_USER;
            if (phdr->p_flags & PF_W) {
                flags |= PAGE_WRITABLE;
            }

            if (vmm_map_page(process->page_directory, vaddr, paddr, flags) < 0) {
                terminal_writestring("ELF: Failed to map page\n");
                pmm_free_page(phys_page);
                return -1;
            }

            process->pages_allocated++;
        }

        // Copy data
        if (phdr->p_filesz > 0) {
            // Bounds check: ensure segment data is within the file
            if (phdr->p_offset + phdr->p_filesz > size) {
                terminal_writestring("ELF: Segment data extends beyond file\n");
                return -1;
            }

            uint8_t* src = (uint8_t*)elf_data + phdr->p_offset;

            // Copy page by page (since physical pages might not be contiguous)
            for (uint32_t off = 0; off < phdr->p_filesz; ) {
                uint32_t vaddr = phdr->p_vaddr + off;
                uint32_t paddr = vmm_get_physical(process->page_directory, vaddr);
                if (paddr == 0) {
                    terminal_writestring("ELF: Failed to get physical address\n");
                    return -1;
                }

                uint32_t page_off = vaddr & (PAGE_SIZE - 1);
                uint32_t copy_size = PAGE_SIZE - page_off;

                if (off + copy_size > phdr->p_filesz) {
                    copy_size = phdr->p_filesz - off;
                }

                memcpy((void*)(paddr + page_off), src + off, copy_size);
                off += copy_size;
            }
        }

        // Zero BSS section (memsz > filesz)
        if (phdr->p_memsz > phdr->p_filesz) {
            uint32_t bss_start = phdr->p_vaddr + phdr->p_filesz;
            uint32_t bss_size = phdr->p_memsz - phdr->p_filesz;

            // Zero it out
            for (uint32_t off = 0; off < bss_size; ) {
                uint32_t vaddr = bss_start + off;
                uint32_t paddr = vmm_get_physical(process->page_directory, vaddr);
                if (paddr == 0) {
                    terminal_writestring("ELF: Failed to get physical address for BSS\n");
                    return -1;
                }

                uint32_t page_off = vaddr & (PAGE_SIZE - 1);
                uint32_t zero_size = PAGE_SIZE - page_off;

                if (off + zero_size > bss_size) {
                    zero_size = bss_size - off;
                }

                memset((void*)(paddr + page_off), 0, zero_size);
                off += zero_size;
            }
        }

        if (segment_end > highest_loaded) {
            highest_loaded = segment_end;
        }
    }

    process->user_entry = header->e_entry;
    if (process->heap_start < PAGE_ALIGN_UP(highest_loaded)) {
        process->heap_start = PAGE_ALIGN_UP(highest_loaded);
        process->heap_current = process->heap_start;
    }

    return 0;
}

// Create process from ELF
process_t* elf_create_process(void* elf_data, size_t size, const char* name) {
    process_t* process = process_create_user(name, 1);
    if (!process) {
        return NULL;
    }

    // Load ELF
    if (elf_load(process, elf_data, size) < 0) {
        process_destroy(process);
        return NULL;
    }

    ELF_DEBUG("ELF: Process '");
    ELF_DEBUG(name);
    ELF_DEBUG("' created\n");

    return process;
}
