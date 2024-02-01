/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * jitc.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include "system.h"
#include "jitc.h"

/**
 * Needs:
 *   fork()
 *   execv()
 *   waitpid()
 *   WIFEXITED()
 *   WEXITSTATUS()
 *   dlopen()
 *   dlclose()
 *   dlsym()
 */

/* research the above Needed API and design accordingly */


int jitc_compile(const char *input, const char *output) {
    const char *gcc_path = "/usr/bin/gcc";
    const char *shared_option = "-shared";
    const char *output_option = "-o";
    const char *main_file = "main.o";

    char **compiler_args;
    int num_args = 8;

    pid_t pid;
    int status;

    compiler_args = (char **)malloc((num_args + 1) * sizeof(char *));
    if (compiler_args == NULL) {
        perror("malloc");
        return -1;
    }

    compiler_args[0] = (char *)gcc_path;
    compiler_args[1] = "-O3";
    compiler_args[2] = "-fpic";
    compiler_args[3] = (char *)shared_option;
    compiler_args[4] = (char *)output_option;
    compiler_args[5] = (char *)output;
    compiler_args[6] = (char *)input;
    compiler_args[7] = (char *)main_file;
    compiler_args[8] = NULL;

    if ((pid = fork()) == 0) {
        /* Child process */
        printf("Executing compiler with arguments:\n");
        
        execv(compiler_args[0], compiler_args);
        perror("execv");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        /* Fork failed */
        perror("fork");
        free(compiler_args);
        return -1;
    } else {
        /* Parent process */
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            free(compiler_args);
            return -1;
        }
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            if (exit_status != 0) {
                TRACE("Compilation failed");
                free(compiler_args);
                return -1;
            } else {
                printf("Compilation succeeded. Output file: %s\n", output);
            }
        } else {
            TRACE("Compilation did not complete");
            free(compiler_args);
            return -1;
        }
    }

    free(compiler_args);
    return 0;
}


struct jitc {
    void *module;  /* Handle to the dynamically loaded module*/
};

struct jitc *jitc_open(const char *pathname) {
    struct jitc *jitc = malloc(sizeof(struct jitc));
    if (!jitc) {
        TRACE("Out of memory");
        return NULL;
    }

    jitc->module = dlopen(pathname, RTLD_LAZY | RTLD_LOCAL);
    if (!jitc->module) {
        const char *dl_error = dlerror();  /* Get dynamic loading error message */
        if (dl_error) {
            fprintf(stderr, "dlopen error: %s\n", dl_error);
        } else {
            fprintf(stderr, "Unknown dlopen error\n");
        }
        free(jitc);
        return NULL;
    }

    return jitc;
}

void jitc_close(struct jitc *jitc) {
    if (jitc) {
        if (jitc->module) {
            dlclose(jitc->module);
        }
        free(jitc);
    }
}

long jitc_lookup(struct jitc *jitc, const char *symbol) {

     /* Look up the symbol within the loaded module */
    void *address;

    if (!jitc || !jitc->module) {
        return 0;
    }

   address = dlsym( jitc->module, symbol);

    /* Return the memory address*/
    return (long)address;
}
