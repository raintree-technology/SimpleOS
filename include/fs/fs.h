#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

#define FS_FILENAME_MAX 32
#define FS_MAX_FILES 64
#define FS_BLOCK_SIZE 512
#define FS_MAX_BLOCKS 1024

// File types
#define FS_TYPE_FILE 1
#define FS_TYPE_DIR  2

// File permissions
#define FS_PERM_READ  0x4
#define FS_PERM_WRITE 0x2
#define FS_PERM_EXEC  0x1

// File structure
typedef struct {
    char name[FS_FILENAME_MAX];
    uint32_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t first_block;
    uint32_t created_time;
    uint32_t modified_time;
    uint32_t inode_num;
} fs_node_t;

// Directory entry
typedef struct {
    char name[FS_FILENAME_MAX];
    uint32_t inode;
} fs_dirent_t;

// RAM filesystem structures
typedef struct {
    uint8_t data[FS_BLOCK_SIZE];
    uint32_t next_block;  // -1 for end of chain
} fs_block_t;

typedef struct {
    fs_node_t nodes[FS_MAX_FILES];
    fs_block_t blocks[FS_MAX_BLOCKS];
    uint32_t free_blocks[FS_MAX_BLOCKS / 32];  // Bitmap
    uint32_t next_inode;
    uint32_t next_free_hint;  // Hint for faster block allocation
    fs_node_t* root;
} ramfs_t;

// Global filesystem functions
void fs_init(void);
fs_node_t* fs_root(void);

// File operations
int fs_read(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int fs_write(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
void fs_open(fs_node_t* node);
void fs_close(fs_node_t* node);
fs_dirent_t* fs_readdir(fs_node_t* node, uint32_t index);
fs_node_t* fs_finddir(fs_node_t* node, char* name);
fs_node_t* fs_resolve_path(const char* path);

// RAM filesystem specific
fs_node_t* ramfs_create_file(fs_node_t* parent, const char* name);
fs_node_t* ramfs_create_dir(fs_node_t* parent, const char* name);
fs_node_t* ramfs_create_file_path(const char* path);
fs_node_t* ramfs_create_dir_path(const char* path);
int ramfs_delete(fs_node_t* parent, const char* name);

#endif
