/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scm.c
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "scm.h"
#include "system.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define VIRT_ADDR 0x600000000000

struct scm {
    int fd;
    void *memory;
    size_t length; /*File complete size - capacity*/
    size_t size; /*Current occupied size - utilized*/
};

struct scm *scm_open(const char *pathname, int truncate) {
    struct scm *scm = malloc(sizeof(struct scm));
    struct stat fileInfo = {0};
    
    if (!scm) {
        perror("Failed to allocate scm structure");
        return NULL;
    }

    scm->fd = open(pathname, O_RDWR);
    if (scm->fd == -1) {
        perror("Error opening file for writing");
        free(scm);
        return NULL;
    }

    if (fstat(scm->fd, &fileInfo) == -1) {
        perror("Error getting the file info");
        close(scm->fd);
        free(scm);
        return NULL;
    }
    
    if (!S_ISREG(fileInfo.st_mode)) {
        perror("Error: file is not a regular file");
        close(scm->fd);
        free(scm);
        return NULL;
    }
    
    scm->length = fileInfo.st_size; /*Store current size of file in length*/

    if (truncate) {
        if (ftruncate(scm->fd,scm->length) == -1) {
            perror("Error truncating file");
            close(scm->fd);
            free(scm);
            return NULL;
        }
    } else {
        scm->length = fileInfo.st_size;
    }

    scm->size = 0;

    scm->memory = mmap((void *)VIRT_ADDR, scm->length, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, scm->fd, 0);
    if (scm->memory == MAP_FAILED) {
        perror("Error mmapping the file");
        close(scm->fd);
        free(scm);
        return NULL;
    }else{
        printf("Mapped address: %p\n",  scm->memory); 
    }

    return scm;
}

void scm_close(struct scm *scm) {
    if (scm) {
        msync(scm->memory, scm->length, MS_SYNC);
        munmap(scm->memory, scm->length);
        close(scm->fd);
        free(scm);
    }
}

void *scm_malloc(struct scm *scm, size_t size) {
    void *allocated_memory;

    if (scm->size + size > scm->length) {
        perror("Not enough space - scm_malloc");
        return NULL;
    }

    allocated_memory = (char *)scm->memory + scm->size;
    scm->size += size; 

    msync(allocated_memory, size, MS_SYNC);
    
    return allocated_memory;
}

char *scm_strdup(struct scm *scm, const char *s) {
    size_t len;
     void *new_str;
    if (!s) {
        return NULL;
    }

    len = strlen(s) + 1;
    new_str = (char *)scm_malloc(scm, len);
    if (new_str) {
        memcpy(new_str, s, len);
    }
    return new_str;
}

/*void scm_free(struct scm *scm, void *p) {
    Currently, this function doesn't do anything as we don't have
     a mechanism to reclaim memory. This would need to be implemented
    if the system requires memory to be freed and reused.
}*/

size_t scm_utilized(const struct scm *scm) {
    return scm->size;
}

size_t scm_capacity(const struct scm *scm) {
    return scm->length;
}

void *scm_mbase(struct scm *scm) {
    return scm->memory;
}

