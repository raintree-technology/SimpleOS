#include "../include/fs.h"
#include "../include/string.h"
#include "../include/kmalloc.h"
#include "../include/terminal.h"

// Global RAM filesystem
static ramfs_t ramfs;
static fs_ops_t ramfs_ops;

// Forward declarations
static int ramfs_read(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static int ramfs_write(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static void ramfs_open(fs_node_t* node);
static void ramfs_close(fs_node_t* node);
static fs_dirent_t* ramfs_readdir(fs_node_t* node, uint32_t index);
static fs_node_t* ramfs_finddir(fs_node_t* node, char* name);

// Block management
static int allocate_block(void) {
    for (uint32_t i = 0; i < FS_MAX_BLOCKS; i++) {
        uint32_t word = i / 32;
        uint32_t bit = i % 32;
        
        if (!(ramfs.free_blocks[word] & (1 << bit))) {
            ramfs.free_blocks[word] |= (1 << bit);
            ramfs.blocks[i].next_block = -1;
            return i;
        }
    }
    return -1;  // No free blocks
}

static void free_block(uint32_t block) {
    if (block >= FS_MAX_BLOCKS) return;
    
    uint32_t word = block / 32;
    uint32_t bit = block % 32;
    ramfs.free_blocks[word] &= ~(1 << bit);
}

// Initialize filesystem
void fs_init(void) {
    // Clear all structures
    for (int i = 0; i < FS_MAX_FILES; i++) {
        ramfs.nodes[i].inode_num = 0;  // 0 = free
    }
    
    for (int i = 0; i < FS_MAX_BLOCKS / 32; i++) {
        ramfs.free_blocks[i] = 0;
    }
    
    // Set up ops
    ramfs_ops.read = ramfs_read;
    ramfs_ops.write = ramfs_write;
    ramfs_ops.open = ramfs_open;
    ramfs_ops.close = ramfs_close;
    ramfs_ops.readdir = ramfs_readdir;
    ramfs_ops.finddir = ramfs_finddir;
    
    // Create root directory
    ramfs.next_inode = 1;
    ramfs.nodes[0].inode_num = ramfs.next_inode++;
    strcpy(ramfs.nodes[0].name, "/");
    ramfs.nodes[0].type = FS_TYPE_DIR;
    ramfs.nodes[0].size = 0;
    ramfs.nodes[0].permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    ramfs.nodes[0].first_block = allocate_block();
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
    
    terminal_writestring("Filesystem initialized\n");
}

fs_node_t* fs_root(void) {
    return ramfs.root;
}

// Generic filesystem operations
int fs_read(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    return ramfs_ops.read(node, offset, size, buffer);
}

int fs_write(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    return ramfs_ops.write(node, offset, size, buffer);
}

void fs_open(fs_node_t* node) {
    ramfs_ops.open(node);
}

void fs_close(fs_node_t* node) {
    ramfs_ops.close(node);
}

fs_dirent_t* fs_readdir(fs_node_t* node, uint32_t index) {
    return ramfs_ops.readdir(node, index);
}

fs_node_t* fs_finddir(fs_node_t* node, char* name) {
    return ramfs_ops.finddir(node, name);
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
    
    // Allocate blocks as needed
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
            // Need new block
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
    
    // Update file size
    if (offset + bytes_written > node->size) {
        node->size = offset + bytes_written;
    }
    
    return bytes_written;
}

static void ramfs_open(fs_node_t* node) {
    // Nothing to do for RAM fs
    (void)node;
}

static void ramfs_close(fs_node_t* node) {
    // Nothing to do for RAM fs
    (void)node;
}

static fs_dirent_t* ramfs_readdir(fs_node_t* node, uint32_t index) {
    if (node->type != FS_TYPE_DIR) return 0;
    
    static fs_dirent_t dirent;
    uint32_t count = 0;
    
    // Read directory entries from first block
    if (node->first_block != (uint32_t)-1) {
        fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[node->first_block].data;
        uint32_t max_entries = FS_BLOCK_SIZE / sizeof(fs_dirent_t);
        
        for (uint32_t i = 0; i < max_entries; i++) {
            if (entries[i].inode != 0) {
                if (count == index) {
                    memcpy(&dirent, &entries[i], sizeof(fs_dirent_t));
                    return &dirent;
                }
                count++;
            }
        }
    }
    
    return 0;
}

static fs_node_t* ramfs_finddir(fs_node_t* node, char* name) {
    if (node->type != FS_TYPE_DIR) return 0;
    
    // Read directory entries from first block
    if (node->first_block != (uint32_t)-1) {
        fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[node->first_block].data;
        uint32_t max_entries = FS_BLOCK_SIZE / sizeof(fs_dirent_t);
        
        for (uint32_t i = 0; i < max_entries; i++) {
            if (entries[i].inode != 0 && strcmp(entries[i].name, name) == 0) {
                // Find the node with this inode
                for (int j = 0; j < FS_MAX_FILES; j++) {
                    if (ramfs.nodes[j].inode_num == entries[i].inode) {
                        return &ramfs.nodes[j];
                    }
                }
            }
        }
    }
    
    return 0;
}

// Create a new file
fs_node_t* ramfs_create_file(fs_node_t* parent, const char* name) {
    if (parent->type != FS_TYPE_DIR) return 0;
    
    // Find free node
    int free_node = -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ramfs.nodes[i].inode_num == 0) {
            free_node = i;
            break;
        }
    }
    
    if (free_node == -1) return 0;
    
    // Initialize node
    fs_node_t* node = &ramfs.nodes[free_node];
    node->inode_num = ramfs.next_inode++;
    strcpy(node->name, name);
    node->type = FS_TYPE_FILE;
    node->size = 0;
    node->permissions = FS_PERM_READ | FS_PERM_WRITE;
    node->first_block = -1;
    
    // Add to parent directory
    if (parent->first_block != (uint32_t)-1) {
        fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[parent->first_block].data;
        uint32_t max_entries = FS_BLOCK_SIZE / sizeof(fs_dirent_t);
        
        for (uint32_t i = 0; i < max_entries; i++) {
            if (entries[i].inode == 0) {
                strcpy(entries[i].name, name);
                entries[i].inode = node->inode_num;
                parent->size++;
                break;
            }
        }
    }
    
    return node;
}

// Create a new directory
fs_node_t* ramfs_create_dir(fs_node_t* parent, const char* name) {
    if (parent->type != FS_TYPE_DIR) return 0;
    
    // Find free node
    int free_node = -1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ramfs.nodes[i].inode_num == 0) {
            free_node = i;
            break;
        }
    }
    
    if (free_node == -1) return 0;
    
    // Initialize node
    fs_node_t* node = &ramfs.nodes[free_node];
    node->inode_num = ramfs.next_inode++;
    strcpy(node->name, name);
    node->type = FS_TYPE_DIR;
    node->size = 0;
    node->permissions = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
    node->first_block = allocate_block();
    
    if (node->first_block == (uint32_t)-1) {
        node->inode_num = 0;
        return 0;
    }
    
    // Clear directory block
    memset(ramfs.blocks[node->first_block].data, 0, FS_BLOCK_SIZE);
    
    // Add to parent directory
    if (parent->first_block != (uint32_t)-1) {
        fs_dirent_t* entries = (fs_dirent_t*)ramfs.blocks[parent->first_block].data;
        uint32_t max_entries = FS_BLOCK_SIZE / sizeof(fs_dirent_t);
        
        for (uint32_t i = 0; i < max_entries; i++) {
            if (entries[i].inode == 0) {
                strcpy(entries[i].name, name);
                entries[i].inode = node->inode_num;
                parent->size++;
                break;
            }
        }
    }
    
    return node;
}