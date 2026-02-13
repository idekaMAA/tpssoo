#ifndef MENSAJES_STORAGE_H
#define MENSAJES_STORAGE_H

#include <stdint.h>
#include "proto.h"

#define W_PATH_MAX M_MAX_PATH

typedef struct {
    uint32_t block_size;
} t_block_size;

// -------------------- READ --------------------
typedef struct {
    uint32_t query_id;
    char path[M_MAX_PATH];
    uint32_t block_idx;
} t_read_req;

// -------------------- WRITE -------------------
typedef struct {
    uint32_t query_id;
    char path[M_MAX_PATH];
    uint32_t block_idx;
    uint32_t len;
} t_write_req;

// -------------------- TRUNCATE ----------------
typedef struct __attribute__((__packed__)) {
    uint32_t query_id;
    char path[W_PATH_MAX];
    uint32_t new_size;
} t_truncate_req;

// -------------------- CREATE ------------------
typedef struct __attribute__((__packed__)) {
    uint32_t query_id;
    char path[W_PATH_MAX];
} t_create_req;

// -------------------- DELETE ------------------
typedef struct {
    uint32_t query_id;
    char path[M_MAX_PATH];
} t_delete_req;

// -------------------- TAG ---------------------
typedef struct {
    uint32_t query_id;
    char src[W_PATH_MAX];
    char dst[W_PATH_MAX];
} t_tag_req;

// -------------------- COMMIT ------------------
typedef struct __attribute__((__packed__)) {
    uint32_t query_id;
    char path[W_PATH_MAX];
} t_commit_req;

#endif