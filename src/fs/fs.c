#include "fs/fs.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "drivers/terminal.h"
#include "../../userspace/hello_binary.h"
#include "../../userspace/shell_binary.h"
#include "../../userspace/grep_binary.h"
#include "../../userspace/wc_binary.h"

// Global RAM filesystem
static ramfs_t ramfs;

// Forward declarations
static int ramfs_read(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static int ramfs_write(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static void ramfs_open(fs_node_t* node);
static void ramfs_close(fs_node_t* node);
static fs_dirent_t* ramfs_readdir(fs_node_t* node, uint32_t index);
static fs_node_t* ramfs_finddir(fs_node_t* node, char* name);

static int component_from_path(const char** path, char* component) {
    size_t len = 0;
    const char* p = *path;

    while (*p == '/') {
        p++;
    }

    if (*p == '\0') {
        *path = p;
        return 0;
    }

    while (*p && *p != '/') {
        if (len >= FS_FILENAME_MAX - 1) {
            return -1;
        }
        component[len++] = *p++;
    }

    component[len] = '\0';
    *path = p;
    return 1;
}

static int validate_name(const char* name) {
    size_t len = 0;

    if (!name || name[0] == '\0') {
        return 0;
    }

    while (name[len]) {
        if (name[len] == '/' || len >= FS_FILENAME_MAX - 1) {
            return 0;
        }
        len++;
    }

    return 1;
}

static fs_node_t* resolve_parent_dir(const char* path, char* leaf_name) {
    const char* cursor = path;
    fs_node_t* current = ramfs.root;
    char component[FS_FILENAME_MAX];
    int rc;

    if (!current || !path) {
        return NULL;
    }

    while (1) {
        rc = component_from_path(&cursor, component);
        if (rc <= 0) {
            return NULL;
        }

        while (*cursor == '/') {
            cursor++;
        }

        if (*cursor == '\0') {
            strncpy(leaf_name, component, FS_FILENAME_MAX - 1);
            leaf_name[FS_FILENAME_MAX - 1] = '\0';
            return current;
        }

        current = ramfs_finddir(current, component);
        if (!current || current->type != FS_TYPE_DIR) {
            return NULL;
        }
    }
}

// Block management with allocation hint
static int allocate_block(void) {
    for (uint32_t n = 0; n < FS_MAX_BLOCKS; n++) {
        uint32_t i = (ramfs.next_free_hint + n) % FS_MAX_BLOCKS;
        uint32_t word = i / 32;
        uint32_t bit = i % 32;

        if (!(ramfs.free_blocks[word] & (1 << bit))) {
            ramfs.free_blocks[word] |= (1 << bit);
            ramfs.blocks[i].next_block = (uint32_t)-1;
            ramfs.next_free_hint = (i + 1) % FS_MAX_BLOCKS;
            return i;
        }
    }
    return -1;
}

static void free_block(uint32_t block) {
    if (block >= FS_MAX_BLOCKS) return;

    uint32_t word = block / 32;
    uint32_t bit = block % 32;
    ramfs.free_blocks[word] &= ~(1 << bit);

    if (block < ramfs.next_free_hint) {
        ramfs.next_free_hint = block;
    }
}

// Free an entire block chain starting at block_num
static void free_block_chain(uint32_t block_num) {
    while (block_num != (uint32_t)-1) {
        uint32_t next = ramfs.blocks[block_num].next_block;
        free_block(block_num);
        block_num = next;
    }
}

// Look up a node by inode number
static fs_node_t* node_by_inode(uint32_t inode) {
    if (inode == 0) return NULL;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ramfs.nodes[i].inode_num == inode) {
            return &ramfs.nodes[i];
        }
    }
    return NULL;
}

// Iterate directory entries across multiple blocks, calling cb for each.
// If cb returns non-zero, iteration stops and returns a pointer to that entry.
typedef int (*dir_entry_cb)(fs_dirent_t* entry, uint32_t count, void* ctx);

static fs_dirent_t* dir_iterate(fs_node_t* dir, dir_entry_cb cb, void* ctx) {
    if (dir->type != FS_TYPE_DIR) return NULL;

    uint32_t block_num = dir->first_block;
    uint32_t count = 0;
    uint32_t max_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);

    while (block_num != (uint32_t)-1) {
        fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[block_num].data;
        for (uint32_t i = 0; i < max_per_block; i++) {
            if (cb(&entries[i], count, ctx)) {
                return &entries[i];
            }
            if (entries[i].inode != 0) {
                count++;
            }
        }
        block_num = ramfs.blocks[block_num].next_block;
    }
    return NULL;
}

// Initialize filesystem
void fs_init(void) {
    // Clear all structures
    for (int i = 0; i < FS_MAX_FILES; i++) {
        ramfs.nodes[i].inode_num = 0;
    }

    for (int i = 0; i < FS_MAX_BLOCKS / 32; i++) {
        ramfs.free_blocks[i] = 0;
    }

    ramfs.next_free_hint = 0;

    // Create root directory
    ramfs.next_inode = 1;
    ramfs.nodes[0].inode_num = ramfs.next_inode++;
    strcpy(ramfs.nodes[0].name, "/");
    ramfs.nodes[0].type = FS_TYPE_DIR;
    ramfs.nodes[0].size = 0;
    ramfs.nodes[0].permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    ramfs.nodes[0].first_block = allocate_block();
    memset(ramfs.blocks[ramfs.nodes[0].first_block].data, 0, FS_BLOCK_SIZE);
    ramfs.root = &ramfs.nodes[0];

    // Create some initial files
    ramfs_create_file(ramfs.root, "hello.txt");
    fs_node_t* hello = ramfs_finddir(ramfs.root, "hello.txt");
    if (hello) {
        const char* msg = "Hello from SimpleOS filesystem!\n";
        ramfs_write(hello, 0, strlen(msg), (uint8_t*)msg);
    }

    ramfs_create_file(ramfs.root, "readme.txt");
    fs_node_t* readme = ramfs_finddir(ramfs.root, "readme.txt");
    if (readme) {
        const char* msg = "This is a simple RAM-based filesystem.\nFiles are stored in memory.\n";
        ramfs_write(readme, 0, strlen(msg), (uint8_t*)msg);
    }

    fs_node_t* bin = ramfs_create_dir(ramfs.root, "bin");
    if (bin) {
        fs_node_t* hello_bin = ramfs_create_file(bin, "hello");
        if (hello_bin) {
            ramfs_write(hello_bin, 0, hello_elf_len, (uint8_t*)hello_elf);
        }

        if (shell_elf_len > 0) {
            fs_node_t* shell_bin = ramfs_create_file(bin, "shell");
            if (shell_bin) {
                ramfs_write(shell_bin, 0, shell_elf_len, (uint8_t*)shell_elf);
            }
        }

        if (grep_elf_len > 0) {
            fs_node_t* grep_bin = ramfs_create_file(bin, "grep");
            if (grep_bin) {
                ramfs_write(grep_bin, 0, grep_elf_len, (uint8_t*)grep_elf);
            }
        }

        if (wc_elf_len > 0) {
            fs_node_t* wc_bin = ramfs_create_file(bin, "wc");
            if (wc_bin) {
                ramfs_write(wc_bin, 0, wc_elf_len, (uint8_t*)wc_elf);
            }
        }
    }

    terminal_writestring("Filesystem initialized\n");
}

fs_node_t* fs_root(void) {
    return ramfs.root;
}

// Generic filesystem operations (direct calls, no vtable indirection)
int fs_read(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    return ramfs_read(node, offset, size, buffer);
}

int fs_write(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    return ramfs_write(node, offset, size, buffer);
}

void fs_open(fs_node_t* node) {
    ramfs_open(node);
}

void fs_close(fs_node_t* node) {
    ramfs_close(node);
}

fs_dirent_t* fs_readdir(fs_node_t* node, uint32_t index) {
    return ramfs_readdir(node, index);
}

fs_node_t* fs_finddir(fs_node_t* node, char* name) {
    return ramfs_finddir(node, name);
}

fs_node_t* fs_resolve_path(const char* path) {
    const char* cursor = path;
    fs_node_t* current = ramfs.root;
    char component[FS_FILENAME_MAX];
    int rc;

    if (!current || !path) {
        return NULL;
    }

    while (*cursor == '/') {
        cursor++;
    }

    if (*cursor == '\0') {
        return current;
    }

    while ((rc = component_from_path(&cursor, component)) > 0) {
        current = ramfs_finddir(current, component);
        if (!current) {
            return NULL;
        }
    }

    return (rc < 0) ? NULL : current;
}

// RAM filesystem implementation
static int ramfs_read(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node->type != FS_TYPE_FILE) return -1;
    if (offset >= node->size) return 0;

    if (offset + size > node->size) {
        size = node->size - offset;
    }

    uint32_t block_num = node->first_block;
    uint32_t block_offset = offset;

    // Skip to starting block
    while (block_offset >= FS_BLOCK_SIZE && block_num != (uint32_t)-1) {
        block_offset -= FS_BLOCK_SIZE;
        block_num = ramfs.blocks[block_num].next_block;
    }

    uint32_t bytes_read = 0;
    while (bytes_read < size && block_num != (uint32_t)-1) {
        uint32_t to_read = FS_BLOCK_SIZE - block_offset;
        if (to_read > size - bytes_read) {
            to_read = size - bytes_read;
        }

        memcpy(buffer + bytes_read,
               ramfs.blocks[block_num].data + block_offset,
               to_read);

        bytes_read += to_read;
        block_offset = 0;
        block_num = ramfs.blocks[block_num].next_block;
    }

    return bytes_read;
}

static int ramfs_write(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node->type != FS_TYPE_FILE) return -1;

    // Allocate first block if needed
    if (node->first_block == (uint32_t)-1) {
        node->first_block = allocate_block();
        if (node->first_block == (uint32_t)-1) return -1;
    }

    uint32_t block_num = node->first_block;
    uint32_t block_offset = offset;

    // Skip to starting block
    while (block_offset >= FS_BLOCK_SIZE) {
        block_offset -= FS_BLOCK_SIZE;

        if (ramfs.blocks[block_num].next_block == (uint32_t)-1) {
            int new_block = allocate_block();
            if (new_block == -1) return -1;
            ramfs.blocks[block_num].next_block = new_block;
        }

        block_num = ramfs.blocks[block_num].next_block;
    }

    uint32_t bytes_written = 0;
    while (bytes_written < size) {
        uint32_t to_write = FS_BLOCK_SIZE - block_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }

        memcpy(ramfs.blocks[block_num].data + block_offset,
               buffer + bytes_written,
               to_write);

        bytes_written += to_write;
        block_offset = 0;

        if (bytes_written < size) {
            if (ramfs.blocks[block_num].next_block == (uint32_t)-1) {
                int new_block = allocate_block();
                if (new_block == -1) break;
                ramfs.blocks[block_num].next_block = new_block;
            }
            block_num = ramfs.blocks[block_num].next_block;
        }
    }

    uint32_t new_end = offset + bytes_written;

    // Truncate: free trailing blocks when writing from offset 0
    if (offset == 0 && new_end < node->size) {
        uint32_t needed_blocks = (new_end + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
        if (needed_blocks == 0) {
            free_block_chain(node->first_block);
            node->first_block = (uint32_t)-1;
        } else {
            uint32_t b = node->first_block;
            for (uint32_t i = 1; i < needed_blocks; i++) {
                b = ramfs.blocks[b].next_block;
            }
            free_block_chain(ramfs.blocks[b].next_block);
            ramfs.blocks[b].next_block = (uint32_t)-1;
        }
    }

    if (new_end > node->size || (offset == 0 && new_end < node->size)) {
        node->size = new_end;
    }

    return bytes_written;
}

static void ramfs_open(fs_node_t* node) {
    (void)node;
}

static void ramfs_close(fs_node_t* node) {
    (void)node;
}

// Callback context for readdir
struct readdir_ctx { uint32_t target; uint32_t count; };

static int readdir_cb(fs_dirent_t* entry, uint32_t count, void* ctx) {
    (void)count;
    struct readdir_ctx* rc = (struct readdir_ctx*)ctx;
    if (entry->inode != 0) {
        if (rc->count == rc->target) return 1;
        rc->count++;
    }
    return 0;
}

static fs_dirent_t* ramfs_readdir(fs_node_t* node, uint32_t index) {
    if (node->type != FS_TYPE_DIR) return NULL;

    static fs_dirent_t dirent;
    struct readdir_ctx ctx = { .target = index, .count = 0 };
    fs_dirent_t* found = dir_iterate(node, readdir_cb, &ctx);
    if (found) {
        memcpy(&dirent, found, sizeof(fs_dirent_t));
        return &dirent;
    }
    return NULL;
}

// Callback context for finddir
struct finddir_ctx { const char* name; };

static int finddir_cb(fs_dirent_t* entry, uint32_t count, void* ctx) {
    (void)count;
    struct finddir_ctx* fc = (struct finddir_ctx*)ctx;
    return (entry->inode != 0 && strcmp(entry->name, fc->name) == 0);
}

static fs_node_t* ramfs_finddir(fs_node_t* node, char* name) {
    if (node->type != FS_TYPE_DIR) return NULL;

    struct finddir_ctx ctx = { .name = name };
    fs_dirent_t* found = dir_iterate(node, finddir_cb, &ctx);
    if (found) {
        return node_by_inode(found->inode);
    }
    return NULL;
}

// Find a free dirent slot in a directory, extending with a new block if needed
static fs_dirent_t* dir_alloc_entry(fs_node_t* dir) {
    uint32_t block_num = dir->first_block;
    uint32_t max_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);

    uint32_t last_block = (uint32_t)-1;
    while (block_num != (uint32_t)-1) {
        fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[block_num].data;
        for (uint32_t i = 0; i < max_per_block; i++) {
            if (entries[i].inode == 0) {
                return &entries[i];
            }
        }
        last_block = block_num;
        block_num = ramfs.blocks[block_num].next_block;
    }

    // No free slot — allocate a new block for the directory
    int new_block = allocate_block();
    if (new_block == -1) return NULL;
    memset(ramfs.blocks[new_block].data, 0, FS_BLOCK_SIZE);

    if (last_block != (uint32_t)-1) {
        ramfs.blocks[last_block].next_block = new_block;
    } else {
        dir->first_block = new_block;
    }

    return (fs_dirent_t*)ramfs.blocks[new_block].data;
}

// Create a new file
fs_node_t* ramfs_create_file(fs_node_t* parent, const char* name) {
    if (parent->type != FS_TYPE_DIR) return NULL;
    if (!validate_name(name)) return NULL;
    if (ramfs_finddir(parent, (char*)name)) return NULL;

    int free_node = -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ramfs.nodes[i].inode_num == 0) {
            free_node = i;
            break;
        }
    }
    if (free_node == -1) return NULL;

    fs_dirent_t* slot = dir_alloc_entry(parent);
    if (!slot) return NULL;

    fs_node_t* node = &ramfs.nodes[free_node];
    node->inode_num = ramfs.next_inode++;
    strncpy(node->name, name, FS_FILENAME_MAX - 1);
    node->name[FS_FILENAME_MAX - 1] = '\0';
    node->type = FS_TYPE_FILE;
    node->size = 0;
    node->permissions = FS_PERM_READ | FS_PERM_WRITE;
    node->first_block = (uint32_t)-1;

    strncpy(slot->name, name, FS_FILENAME_MAX - 1);
    slot->name[FS_FILENAME_MAX - 1] = '\0';
    slot->inode = node->inode_num;

    return node;
}

// Create a new directory
fs_node_t* ramfs_create_dir(fs_node_t* parent, const char* name) {
    if (parent->type != FS_TYPE_DIR) return NULL;
    if (!validate_name(name)) return NULL;
    if (ramfs_finddir(parent, (char*)name)) return NULL;

    int free_node = -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ramfs.nodes[i].inode_num == 0) {
            free_node = i;
            break;
        }
    }
    if (free_node == -1) return NULL;

    int dir_block = allocate_block();
    if (dir_block == -1) return NULL;
    memset(ramfs.blocks[dir_block].data, 0, FS_BLOCK_SIZE);

    fs_dirent_t* slot = dir_alloc_entry(parent);
    if (!slot) {
        free_block(dir_block);
        return NULL;
    }

    fs_node_t* node = &ramfs.nodes[free_node];
    node->inode_num = ramfs.next_inode++;
    strncpy(node->name, name, FS_FILENAME_MAX - 1);
    node->name[FS_FILENAME_MAX - 1] = '\0';
    node->type = FS_TYPE_DIR;
    node->size = 0;
    node->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    node->first_block = dir_block;

    strncpy(slot->name, name, FS_FILENAME_MAX - 1);
    slot->name[FS_FILENAME_MAX - 1] = '\0';
    slot->inode = node->inode_num;

    return node;
}

// Delete a file or empty directory
int ramfs_delete(fs_node_t* parent, const char* name) {
    if (!parent || parent->type != FS_TYPE_DIR) return -1;
    if (!validate_name(name)) return -1;

    struct finddir_ctx ctx = { .name = name };
    fs_dirent_t* entry = dir_iterate(parent, finddir_cb, &ctx);
    if (!entry) return -1;

    fs_node_t* node = node_by_inode(entry->inode);
    if (!node) return -1;

    // Refuse to delete non-empty directories
    if (node->type == FS_TYPE_DIR) {
        uint32_t block_num = node->first_block;
        uint32_t max_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);
        while (block_num != (uint32_t)-1) {
            fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[block_num].data;
            for (uint32_t i = 0; i < max_per_block; i++) {
                if (entries[i].inode != 0) return -1;
            }
            block_num = ramfs.blocks[block_num].next_block;
        }
    }

    free_block_chain(node->first_block);

    node->inode_num = 0;
    node->first_block = (uint32_t)-1;
    node->size = 0;
    node->name[0] = '\0';

    entry->inode = 0;
    entry->name[0] = '\0';

    return 0;
}

fs_node_t* ramfs_create_file_path(const char* path) {
    char leaf[FS_FILENAME_MAX];
    fs_node_t* parent = resolve_parent_dir(path, leaf);
    if (!parent) {
        return NULL;
    }
    return ramfs_create_file(parent, leaf);
}

fs_node_t* ramfs_create_dir_path(const char* path) {
    char leaf[FS_FILENAME_MAX];
    fs_node_t* parent = resolve_parent_dir(path, leaf);
    if (!parent) {
        return NULL;
    }
    return ramfs_create_dir(parent, leaf);
}
