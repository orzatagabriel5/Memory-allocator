/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DIE(assertion, call_description)                                                                               \
	do {                                                                                                               \
		if (assertion) {                                                                                               \
			fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                                                         \
			perror(call_description);                                                                                  \
			exit(errno);                                                                                               \
		}                                                                                                              \
	} while (0)

/* Structure to hold memory block metadata */
struct block_meta {
	size_t size;
	int status;
	struct block_meta *next;
};

struct block_meta *block;


/* Block metadata status values */
#define STATUS_FREE   0
#define STATUS_ALLOC  1
#define STATUS_MAPPED 2
#define MMAP_THRESHOLD 128*1024
#define PAGE_SIZE getpagesize()
#define BLOCK_SIZE sizeof(struct block_meta)
#define ALIGNMENT 8

