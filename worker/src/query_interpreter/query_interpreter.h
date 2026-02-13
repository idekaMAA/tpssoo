// ============================================================================
// WORKER - query_interpreter.h
// PASO A PASO GENERAL
// 1) Expone ejecutar_query para correr una Query completa
// 2) Recibe IDs, path, archivo, PC inicial y FDs de Storage/Master
// ============================================================================

#ifndef QUERY_INTERPRETER_H
#define QUERY_INTERPRETER_H

#include <stdint.h>
#include <commons/log.h>

int ejecutar_query(
    uint32_t    query_id,
    const char* queries_path,
    const char* filename,
    uint32_t    pc_inicial,
    t_log*      logger,
    int         fd_storage,
    int         fd_master,
    uint32_t    retardo_ms
);

#endif
