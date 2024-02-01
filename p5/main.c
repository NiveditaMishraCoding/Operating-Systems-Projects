/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * main.c
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "system.h"
#include <string.h>
#include <errno.h>
/**
 * Needs:
 *   signal()
 */

static volatile int done;

static void
_signal_(int signum)
{
	assert( SIGINT == signum );

	done = 1;
}

double
cpu_util(const char *s)
{
	static unsigned sum_, vector_[7];
	unsigned sum, vector[7];
	const char *p;
	double util;
	uint64_t i;

	/*
	  user
	  nice
	  system
	  idle
	  iowait
	  irq
	  softirq
	*/

	if (!(p = strstr(s, " ")) ||
	    (7 != sscanf(p,
			 "%u %u %u %u %u %u %u",
			 &vector[0],
			 &vector[1],
			 &vector[2],
			 &vector[3],
			 &vector[4],
			 &vector[5],
			 &vector[6]))) {
		return 0;
	}
	sum = 0.0;
	for (i=0; i<ARRAY_SIZE(vector); ++i) {
		sum += vector[i];
	}
	util = (1.0 - (vector[3] - vector_[3]) / (double)(sum - sum_)) * 100.0;
	sum_ = sum;
	for (i=0; i<ARRAY_SIZE(vector); ++i) {
		vector_[i] = vector[i];
	}
	return util;
}

/**
 * Reads memory statistics from /proc/meminfo and calculates used memory.
 * 
 * @return Used memory in megabytes.
 */
double get_memory_used() {
    FILE *fp;
    char buffer[256];
    unsigned long mem_total = 0, mem_available = 0, mem_free = 0;
    int mem_total_found = 0, mem_available_found = 0, mem_free_found = 0;
    double memory_used = 0.0;

    /* Open /proc/meminfo file */
    fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        fprintf(stderr, "Error opening /proc/meminfo: %s\n", strerror(errno));
        return -1.0; /* Indicate error */
    }

    /* Read memory info line by line */
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if (!mem_total_found && sscanf(buffer, "MemTotal: %lu kB", &mem_total) == 1) {
            mem_total_found = 1; /* Successfully read MemTotal */
        }
        if (!mem_available_found && sscanf(buffer, "MemAvailable: %lu kB", &mem_available) == 1) {
            mem_available_found = 1; /* Successfully read MemAvailable (more accurate for used memory) */
        }
        if (!mem_free_found && sscanf(buffer, "MemFree: %lu kB", &mem_free) == 1) {
            mem_free_found = 1; /* Successfully read MemFree (use if MemAvailable is not present) */
        }
    }

    fclose(fp);

    /* Calculate used memory */
    if (mem_total_found) {
        if (mem_available_found) {
            memory_used = (mem_total - mem_available) / 1024.0; /* Convert from kB to MB */
        } else if (mem_free_found) {
            memory_used = (mem_total - mem_free) / 1024.0; /* Convert from kB to MB */
        }
    }

    return memory_used;
}
/**
 * Reads network packets statistics from /proc/net/dev.
 * 
 * @param packets_sent Pointer to store the total number of packets sent.
 * @param packets_received Pointer to store the total number of packets received.
 */
void get_network_packets(unsigned long *packets_sent, unsigned long *packets_received) {
    FILE *fp;
    char buffer[1024];
    char iface[20];
    unsigned long rx_packets, tx_packets;
    unsigned long total_rx_packets = 0, total_tx_packets = 0;

    /* Initialize the output parameters */
    *packets_sent = 0;
    *packets_received = 0;

    /* Open /proc/net/dev file */
    fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        fprintf(stderr, "Error opening /proc/net/dev: %s\n", strerror(errno));
        return; 
    }

    /* Read the file line by line */
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        /* Ignore the header lines */
        if (strstr(buffer, "Inter-") || strstr(buffer, "face")) {
            continue;
        }

        /* Parse the statistics 
        The format here assumes that the first column is the interface name followed by various statistics
        We are interested in the third column (received packets) and the eleventh column (transmitted packets)*/
        if (sscanf(buffer, "%s %*s %lu %*s %*s %*s %*s %*s %*s %*s %lu", iface, &rx_packets, &tx_packets) == 3) {
            /* Remove trailing colon from interface name, if present */
            char *colon = strchr(iface, ':');
            if (colon) {
                *colon = '\0';
            }

            /* Accumulate the total packets */
            total_rx_packets += rx_packets;
            total_tx_packets += tx_packets;
        }
    }

    fclose(fp);

    /* Set the output parameters */
    *packets_received = total_rx_packets;
    *packets_sent = total_tx_packets;
}
/**
 * Reads block device I/O statistics from /proc/diskstats.
 * 
 * @param blocks_read Pointer to store the total number of blocks read.
 * @param blocks_written Pointer to store the total number of blocks written.
 */
void get_io_blocks(unsigned long *blocks_read, unsigned long *blocks_written) {
    FILE *fp;
    char buffer[256];
    unsigned long read_sectors, written_sectors;
    unsigned long total_read_sectors = 0, total_written_sectors = 0;
    char device_name[32];
    int items;

    /* Initialize the output parameters */
    *blocks_read = 0;
    *blocks_written = 0;

    /* Open /proc/diskstats file */
    fp = fopen("/proc/diskstats", "r");
    if (!fp) {
        fprintf(stderr, "Error opening /proc/diskstats: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Read the file line by line */
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        /* Parse the statistics for each device */
        items = sscanf(buffer, "%*u %*u %31s %*u %*u %lu %*u %*u %*u %lu %*u %*u %*u %*u",
                       device_name, &read_sectors, &written_sectors);
        if (items == 3) {
            /* Check if the device name starts with 'sd' or 'nvme' */
            if (strncmp(device_name, "sd", 2) == 0 || 
                strncmp(device_name, "nvme", 4) == 0 ) {
                total_read_sectors += read_sectors;
                total_written_sectors += written_sectors;
            }
        }
    }

    fclose(fp);

    /* Set the output parameters assuming 512 bytes per sector */
    *blocks_read = total_read_sectors/2;
    *blocks_written = total_written_sectors/2;
}

int main(int argc, char *argv[]) {
    const char * const PROC_STAT = "/proc/stat";
    char line[1024];
    FILE *file;
    double memory_used;
    unsigned long packets_sent, packets_received;
    unsigned long blocks_read, blocks_written;

	UNUSED(argc);
	UNUSED(argv);
    /* Setup signal handler */
    if (SIG_ERR == signal(SIGINT, _signal_)) {
        perror("Error setting up signal handler");
        return EXIT_FAILURE;
    }

    /* Main loop for system monitoring */
    while (!done) {
        /* Open /proc/stat to read CPU stats */
        file = fopen(PROC_STAT, "r");
        if (!file) {
            perror("Error opening /proc/stat");
            return EXIT_FAILURE;
        }

        /* Read a line and compute CPU utilization */
        if (fgets(line, sizeof(line), file)) {
            fclose(file);
            memory_used = get_memory_used();
            get_network_packets(&packets_sent, &packets_received);
            get_io_blocks(&blocks_read, &blocks_written);

            /* Clear the line and print the statistics */
            printf("\r\033[K");
            printf("%5.1f%% %5.1fMB %lu %lu %lu %lu", cpu_util(line), memory_used, packets_sent, packets_received, blocks_read, blocks_written);
            fflush(stdout);
        } else {
            /* Close file if line read fails */
            fclose(file);
        }

        /* Sleep for half a second */
        us_sleep(500000);
    }

    /* Clear line and print "Done!" when exiting */
    printf("\r\033[KDone!\n");
    fflush(stdout);

    return EXIT_SUCCESS;
}
