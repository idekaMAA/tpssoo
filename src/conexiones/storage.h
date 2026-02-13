// ============================================================================
// WORKER - storage.h
// PASO A PASO GENERAL
// 1) Exponer funciones de interacción con Storage
// 2) Crear / Tag / Truncate / Commit / Delete
// 3) Leer y escribir bloques lógicos
// ============================================================================

#ifndef WORKER_STORAGE_H
#define WORKER_STORAGE_H

#include <stdint.h>
#include <commons/log.h>
#include <stdint.h>

#include "../query_interpreter/instrucciones_parser.h"

// ---------------------------------------------------------------------------
// Contexto de Query (para que Storage reciba query_id en cada instrucción)
// Se define en conexiones/storage.c del Worker como TLS.
// ---------------------------------------------------------------------------
#if defined(__GNUC__)
extern __thread uint32_t g_worker_query_id;
#else
extern uint32_t g_worker_query_id;
#endif

void storage_set_query_id(uint32_t query_id);

// ---------------------------------------------------------------------------
// Block size y handshake
// ---------------------------------------------------------------------------
int storage_handshake_y_blocksize(int fd_storage, uint32_t* block_size_out);
int storage_get_block_size(int fd_storage, t_log* logger);
void storage_set_worker_id(uint32_t worker_id);
void storage_set_query_id(uint32_t query_id);
// ---------------------------------------------------------------------------
// Operaciones sobre archivos (FILE:TAG)
// ---------------------------------------------------------------------------
int storage_create(file_tag_t ft, int fd_storage, t_log* logger);
int storage_tag(file_tag_t origen, file_tag_t destino, int fd_storage, t_log* logger);
int storage_truncate(file_tag_t ft, uint32_t nuevo_tam_bytes, int fd_storage, t_log* logger);
int storage_commit(file_tag_t ft, int fd_storage, t_log* logger);
int storage_delete(file_tag_t ft, int fd_storage, t_log* logger);

// ---------------------------------------------------------------------------
// IO de bloques lógicos
// ---------------------------------------------------------------------------
int storage_io_read_block(
    file_tag_t ft,
    uint32_t   block_id,
    int        fd_storage,
    t_log*     logger,
    char*      destino,
    uint32_t   max_bytes
);

int storage_io_write_block(
    file_tag_t   ft,
    uint32_t     block_id,
    const char*  origen,
    uint32_t     size,
    int          fd_storage,
    t_log*       logger
);

#endif // WORKER_STORAGE_H