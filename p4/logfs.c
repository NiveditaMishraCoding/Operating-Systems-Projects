

/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * logfs.c
 */

/*#include <pthread.h>
#include "device.h"
#include "logfs.h"

#define WCACHE_BLOCKS 32
#define RCACHE_BLOCKS 256*/

/**
 * Needs:
 *   pthread_create()
 *   pthread_join()
 *   pthread_mutex_init()
 *   pthread_mutex_destroy()
 *   pthread_mutex_lock()
 *   pthread_mutex_unlock()a
 *   pthread_cond_init()
 *   pthread_cond_destroy()
 *   pthread_cond_wait()
 *   pthread_cond_signal()
 */

/* research the above Needed API and design accordingly */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "device.h"
#include "system.h"
#include "logfs.h"

#define WCACHE_BLOCKS 32
#define RCACHE_BLOCKS 256


struct read_cache_entry {
    uint64_t offset;  
    char *data;       
    char *unaligned_data; 
    size_t length;    
    int valid;    
};


struct logfs {
    struct device *device;
    char *write_buffer;
    char *unaligned_write_buffer;
    size_t buffer_size;
    size_t head;
    size_t tail;
    pthread_mutex_t buffer_mutex;
    pthread_cond_t buffer_cond;
    pthread_t write_thread;
    int should_exit;
    size_t last_flushed_head;
    size_t tail_offset;
    struct read_cache_entry read_cache[RCACHE_BLOCKS];
    pthread_mutex_t read_mutex;
};

void *write_thread_func(void *arg);
static int flush_write_buffer(struct logfs *logfs);
static size_t read_from_cache(struct read_cache_entry *cache, void *buf, uint64_t off, size_t len);
struct read_cache_entry* select_cache_entry_for_read(struct read_cache_entry cache[], size_t cache_size, uint64_t aligned_offset);

struct logfs *logfs_open(const char *pathname) {
    struct logfs *logfs = malloc(sizeof(struct logfs));
    uint64_t block_size ;
    size_t page_sz;
    void *write_buffer_unaligned;
    void *cache_data_unaligned;
    int i;

    if (!logfs) {
        return NULL;
    }

    logfs->device = device_open(pathname);
    if (!logfs->device) {
        free(logfs);
        return NULL;
    }

    block_size = device_block(logfs->device);
    page_sz = page_size();
    logfs->buffer_size = WCACHE_BLOCKS * block_size;

    write_buffer_unaligned = malloc(logfs->buffer_size + page_sz);
    if (!write_buffer_unaligned) {
        device_close(logfs->device);
        free(logfs);
        return NULL;
    }
    logfs->write_buffer = memory_align(write_buffer_unaligned, page_sz);
    logfs->unaligned_write_buffer = write_buffer_unaligned;
    memset(logfs->write_buffer, 0, logfs->buffer_size);
    pthread_mutex_init(&logfs->buffer_mutex, NULL);
    pthread_cond_init(&logfs->buffer_cond, NULL);
    
    for (i = 0; i < RCACHE_BLOCKS; i++) {
        cache_data_unaligned = malloc(block_size + page_sz);
        if (!cache_data_unaligned) {
            device_close(logfs->device);
            free(logfs);
            return NULL;
        }
        logfs->read_cache[i].data = memory_align(cache_data_unaligned, page_sz);
        logfs->read_cache[i].unaligned_data = cache_data_unaligned; 
        logfs->read_cache[i].length = block_size;
        logfs->read_cache[i].valid = 0;
        logfs->read_cache[i].offset = 0; 

    }
    pthread_mutex_init(&logfs->read_mutex, NULL);
    
    logfs->head = 0;
    logfs->tail = 0;
    logfs->last_flushed_head = 0;
    logfs->should_exit = 0;
    logfs->tail_offset = 0;

    pthread_create(&logfs->write_thread, NULL, write_thread_func, logfs);

    return logfs;
}

void logfs_close(struct logfs *logfs) {
    int i;
    /*printf("close\n");*/
    if (!logfs) {
        return;
    }

    pthread_mutex_lock(&logfs->buffer_mutex);
    logfs->should_exit = 1;
    pthread_cond_signal(&logfs->buffer_cond);
    pthread_mutex_unlock(&logfs->buffer_mutex);

    pthread_join(logfs->write_thread, NULL);

    flush_write_buffer(logfs);
    
    pthread_mutex_lock(&logfs->read_mutex);
    for (i = 0; i < RCACHE_BLOCKS; i++) {
        free(logfs->read_cache[i].unaligned_data);
    }
    pthread_mutex_unlock(&logfs->read_mutex);
    
    free(logfs->unaligned_write_buffer);
    pthread_mutex_destroy(&logfs->buffer_mutex);
    pthread_cond_destroy(&logfs->buffer_cond);
    pthread_mutex_destroy(&logfs->read_mutex);
    device_close(logfs->device);
    free(logfs);
}

int logfs_read(struct logfs *logfs, void *buf, uint64_t off, size_t len) {
    size_t bytes_read_from_cache = 0;
    struct read_cache_entry *entry = NULL;
    size_t block_size = device_block(logfs->device);
    size_t aligned_offset = off - (off % block_size); 
    size_t offset_difference = off - aligned_offset;
    size_t total_length = len + offset_difference;
    size_t aligned_length = ((total_length + block_size - 1) / block_size) * block_size;
    char *buf_offset;
    size_t remaining_len;
    size_t read_len;

    
    /*printf("read with off: %lu and len: %u and  head: %u and tail: %u and block_size: %u and last_flushed_head: %u\n", 
           (unsigned long)off, 
           (unsigned int)len,
           (unsigned int)logfs->head, 
           (unsigned int)logfs->tail,
           (unsigned int)block_size,
            (unsigned int)logfs->last_flushed_head);*/
    flush_write_buffer(logfs);
    
    pthread_mutex_lock(&logfs->read_mutex);
    bytes_read_from_cache = read_from_cache(logfs->read_cache, buf, off, len);

    /* If cache read is less than the requested length, read the remaining part from disk*/
    if (bytes_read_from_cache < len) {
        buf_offset = (char *)buf + bytes_read_from_cache;
        remaining_len = len - bytes_read_from_cache;
        read_len = 0;

        /* Read the remaining data from disk, potentially in multiple blocks*/
        while (remaining_len > 0) {
            aligned_offset = off + bytes_read_from_cache - (off + bytes_read_from_cache) % block_size;
            aligned_length = ((remaining_len + block_size - 1) / block_size) * block_size;

            entry = select_cache_entry_for_read(logfs->read_cache, RCACHE_BLOCKS, aligned_offset);
            if (!entry->valid) {
                /*printf("read from disk\n");*/
                if (device_read(logfs->device, entry->data, aligned_offset, aligned_length) != 0) {
                    pthread_mutex_unlock(&logfs->read_mutex);
                    return -1;
                }
                entry->offset = aligned_offset;
                entry->valid = 1;  
            }

            /* Calculate the amount of data to read from this cache entry*/
            offset_difference = (off + bytes_read_from_cache) - aligned_offset;
            read_len = aligned_length - offset_difference < remaining_len ? 
                       aligned_length - offset_difference : remaining_len;

            /* Copy the data to the user buffer*/
            memcpy(buf_offset, entry->data + offset_difference, read_len);
            buf_offset += read_len;
            bytes_read_from_cache += read_len;
            remaining_len -= read_len;
        }
    }
    pthread_mutex_unlock(&logfs->read_mutex);
    

    return 0;  
}

static int flush_write_buffer(struct logfs *logfs) {
    size_t size_to_write;
    size_t block_size = device_block(logfs->device);
    size_t padding = 0;
    size_t wrap_case_remaining = block_size;
    int i;
    uint64_t start_block;
    size_t available_space;

    pthread_mutex_lock(&logfs->buffer_mutex);
    if (logfs->last_flushed_head == logfs->head) {
        pthread_mutex_unlock(&logfs->buffer_mutex);
       /*printf("head same no flush\n");*/
        return 1;
    }
   
    while(wrap_case_remaining > 0){
        
        start_block = logfs->tail_offset;

        size_to_write = (logfs->head > logfs->tail) ?
                        (logfs->head - logfs->tail) :
                        (logfs->buffer_size - logfs->tail) + logfs->head;

        if(logfs->tail + size_to_write > logfs->buffer_size){
            wrap_case_remaining = (logfs->tail + size_to_write) % logfs->buffer_size;   
            size_to_write -= wrap_case_remaining;
        }else{
            wrap_case_remaining = 0;
        }

        if (size_to_write % block_size != 0) {
            padding = block_size - (size_to_write % block_size);

            available_space = (logfs->tail <= logfs->head) ?
                             (logfs->buffer_size - logfs->head + logfs->tail) :
                             (logfs->tail - logfs->head);
    
            if (available_space < (size_to_write + padding)) {
                printf("no space to pad 0 in buffer\n");
                pthread_mutex_unlock(&logfs->buffer_mutex);
                return -1; 
            }

            memset(logfs->write_buffer + logfs->head, 0, padding);
            size_to_write += padding;
        }

        /*printf("flush write size_to_write: %u and padding: %u and logfs->tail_offset: %u and logfs->tail: %u\n", 
        (unsigned int)size_to_write, (unsigned int)padding,
        (unsigned int)logfs->tail_offset, (unsigned int)logfs->tail);*/

        if (device_write(logfs->device, logfs->write_buffer + logfs->tail, logfs->tail_offset, size_to_write) != 0) {
            printf("flush write failed\n");
            pthread_mutex_unlock(&logfs->buffer_mutex);
            return -1;
        }
        if (padding > 0) {
            logfs->tail = (logfs->tail + size_to_write - block_size) % logfs->buffer_size;
            logfs->tail_offset = (logfs->tail_offset + size_to_write - block_size);
            /*logfs->head = (logfs->tail + size_to_write - padding) % logfs->buffer_size;*/
        } else {
            logfs->tail = (logfs->tail + size_to_write) % logfs->buffer_size;
            logfs->tail_offset = (logfs->tail_offset + size_to_write);
        }

        pthread_mutex_lock(&logfs->read_mutex);
        while(size_to_write > 0){
            for (i = 0; i < RCACHE_BLOCKS; ++i) {
                if (logfs->read_cache[i].valid && start_block >= logfs->read_cache[i].offset && 
                    start_block < logfs->read_cache[i].offset + logfs->read_cache[i].length) {
                   /* printf("Invalidating cache in flush , tail-offset: %u and cache[i].offset: %lu\n", (unsigned int)logfs->tail_offset,
                        (unsigned long)logfs->read_cache[i].offset);*/
                    logfs->read_cache[i].valid = 0;
                    pthread_mutex_unlock(&logfs->read_mutex);
                    break;
                }
                
            }
            size_to_write -= block_size;
        }
        
        pthread_mutex_unlock(&logfs->read_mutex);

    }

    logfs->last_flushed_head = logfs->head;
    pthread_mutex_unlock(&logfs->buffer_mutex);
    
   /*printf("after flush write head: %u and tail: %u and last_flushed_head: %u and tail_offset: %u\n", 
    (unsigned int)logfs->head, 
    (unsigned int)logfs->tail,
    (unsigned int)logfs->last_flushed_head,
    (unsigned int)logfs->tail_offset);*/

    return 0;
}


static size_t read_from_cache(struct read_cache_entry *cache, void *buf, uint64_t off, size_t len) {
    size_t total_bytes_read = 0;
    size_t remaining_len = len;
    uint64_t current_off = off;
    char *current_buf = (char *)buf;
    size_t cache_offset;
    size_t read_len;
    int cache_hit;
    int i;

    while (remaining_len > 0) {
        cache_hit = 0;
        for (i = 0; i < RCACHE_BLOCKS; i++) {
            if (cache[i].valid && current_off >= cache[i].offset && 
                current_off < cache[i].offset + cache[i].length) {
                /* Calculate the offset within the cache entry*/
                cache_offset = current_off - cache[i].offset;
                /* Calculate the length to read from this cache entry*/
                read_len = cache[i].length - cache_offset < remaining_len ? 
                                  cache[i].length - cache_offset : remaining_len;
                /*printf("cache read\n");*/
                memcpy(current_buf, cache[i].data + cache_offset, read_len);
                current_buf += read_len;
                current_off += read_len;
                remaining_len -= read_len;
                total_bytes_read += read_len;
                cache_hit = 1;
                break; 
            }
            
        }
        if (!cache_hit) {
            /* Cache miss, no need to continue searching the cache*/
            break;
        }
    }
    return total_bytes_read;
}

struct read_cache_entry* select_cache_entry_for_read(struct read_cache_entry cache[], size_t cache_size, uint64_t aligned_offset) {
    size_t i;
    struct read_cache_entry* entry_to_evict;
    for (i = 0; i < cache_size; ++i) {
        if (cache[i].offset == aligned_offset && cache[i].valid) {
            /*printf("offset found in cache\n");*/
            return &cache[i];
        }

    }
    /*printf("evict\n");*/
    entry_to_evict = &cache[0];
    entry_to_evict->valid = 0;
    entry_to_evict->offset = aligned_offset;
    return entry_to_evict;
}


int logfs_append(struct logfs *logfs, const void *buf, uint64_t len) {
    size_t available_space;
    size_t first_chunk_size;
    size_t second_chunk_size;
    
    pthread_mutex_lock(&logfs->buffer_mutex);
    
    /*printf("append head: %u and tail: %u and len: %lu\n", 
    (unsigned int)logfs->head, 
    (unsigned int)logfs->tail,
    (unsigned long)len);*/

    available_space = (logfs->tail <= logfs->head) ?
                             (logfs->buffer_size - logfs->head + logfs->tail) :
                             (logfs->tail - logfs->head);
    
    if (available_space < len) {
        printf("no space to write in buffer\n");
        pthread_mutex_unlock(&logfs->buffer_mutex);
        return -1; 
    }

    if (logfs->head + len < logfs->buffer_size) {
        /*printf("no wrapping in append\n");*/
        memcpy(logfs->write_buffer + logfs->head, buf, len);
        logfs->head += len;
    } else {
        /*printf("wrapping in append\n");*/
        first_chunk_size = logfs->buffer_size - logfs->head;
        second_chunk_size = len - first_chunk_size;
        memcpy(logfs->write_buffer + logfs->head, buf, first_chunk_size);
        memcpy(logfs->write_buffer, (char*)buf + first_chunk_size, second_chunk_size);
        logfs->head = second_chunk_size;
    }

    logfs->head = logfs->head % logfs->buffer_size;

    pthread_cond_signal(&logfs->buffer_cond);
    pthread_mutex_unlock(&logfs->buffer_mutex);
    return 0;
}

void *write_thread_func(void *arg) {
    struct logfs *logfs = (struct logfs *)arg;
    size_t size_to_write;
    size_t block_size;
    uint64_t start_block;
    int i;

    block_size = device_block(logfs->device);
    
    while (1) {
        pthread_mutex_lock(&logfs->buffer_mutex);
        
        while (logfs->head == logfs->last_flushed_head && !logfs->should_exit) {
            pthread_cond_wait(&logfs->buffer_cond, &logfs->buffer_mutex);
        }
        
        start_block = logfs->tail_offset;
        
        if (logfs->should_exit) {
            size_to_write = (logfs->head - logfs->tail);
            if (size_to_write % block_size != 0) {
                size_to_write -= size_to_write % block_size; 
                if (size_to_write == 0) {
                    pthread_mutex_unlock(&logfs->buffer_mutex);
                    break; 
                }
            }
            device_write(logfs->device, logfs->write_buffer + logfs->tail, logfs->tail_offset, size_to_write);
            logfs->tail = (logfs->tail + size_to_write) % logfs->buffer_size;
            logfs->tail_offset += size_to_write;
            pthread_mutex_unlock(&logfs->buffer_mutex);
            break;
        }

        size_to_write = (logfs->head > logfs->tail) ?
                               (logfs->head - logfs->tail) :
                               (logfs->buffer_size - logfs->tail);

        if (size_to_write % block_size != 0) {
            size_to_write -= size_to_write % block_size;
            if (size_to_write == 0) {
                pthread_mutex_unlock(&logfs->buffer_mutex);
                continue; 
            }
        }
        /*printf("signalled worker thread write head: %u and tail: %u and last_flushed_head: %u\n", 
        (unsigned int)logfs->head, 
        (unsigned int)logfs->tail,
        (unsigned int)logfs->last_flushed_head);*/
        device_write(logfs->device, logfs->write_buffer + logfs->tail, logfs->tail_offset, size_to_write);
        logfs->tail = (logfs->tail + size_to_write) % logfs->buffer_size;
        logfs->tail_offset += size_to_write;
        logfs->last_flushed_head = logfs->head;

        pthread_mutex_unlock(&logfs->buffer_mutex);

        pthread_mutex_lock(&logfs->read_mutex);
        while(size_to_write > 0){
            for (i = 0; i < RCACHE_BLOCKS; ++i) {
                if (logfs->read_cache[i].valid && start_block >= logfs->read_cache[i].offset && 
                    start_block < logfs->read_cache[i].offset + logfs->read_cache[i].length) {
                    /*printf("Invalidating cache in write , start_block: %lu and cache[i].offset: %lu\n", (unsigned long)start_block,
                        (unsigned long)logfs->read_cache[i].offset);*/
                    logfs->read_cache[i].valid = 0;
                    pthread_mutex_unlock(&logfs->read_mutex);
                    break;
                }
               
            }
            size_to_write -= block_size;
        }
        pthread_mutex_unlock(&logfs->read_mutex);

        
    }

    return NULL;
}



