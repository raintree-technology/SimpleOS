#include "ipc/pipe.h"
#include "mm/kmalloc.h"
#include "kernel/scheduler.h"
#include "lib/string.h"
#include "kernel/process.h"

// Create a new pipe
pipe_t* pipe_create(void) {
    pipe_t* pipe = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!pipe) return NULL;
    
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->reader_closed = 0;
    pipe->writer_closed = 0;
    
    return pipe;
}

// Destroy a pipe
void pipe_destroy(pipe_t* pipe) {
    // In real implementation, would free memory
    // For now, just mark as invalid
    pipe->reader_closed = 1;
    pipe->writer_closed = 1;
}

// Read from pipe
int pipe_read(pipe_t* pipe, void* buffer, size_t count) {
    if (!pipe || pipe->reader_closed) return -1;
    
    uint8_t* buf = (uint8_t*)buffer;
    size_t bytes_read = 0;
    
    while (bytes_read < count) {
        // If pipe is empty
        if (pipe->count == 0) {
            // If writer closed and pipe empty, EOF
            if (pipe->writer_closed) {
                break;
            }
            process_yield();
            continue;
        }
        
        // Read one byte
        buf[bytes_read++] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_SIZE;
        pipe->count--;
    }
    
    return bytes_read;
}

// Write to pipe
int pipe_write(pipe_t* pipe, const void* buffer, size_t count) {
    if (!pipe || pipe->writer_closed) return -1;
    
    // If reader closed, return error (broken pipe)
    if (pipe->reader_closed) return -1;
    
    const uint8_t* buf = (const uint8_t*)buffer;
    size_t bytes_written = 0;
    
    while (bytes_written < count) {
        // If pipe is full
        if (pipe->count == PIPE_SIZE) {
            process_yield();
            continue;
        }
        
        // Write one byte
        pipe->buffer[pipe->write_pos] = buf[bytes_written++];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_SIZE;
        pipe->count++;
    }
    
    return bytes_written;
}
