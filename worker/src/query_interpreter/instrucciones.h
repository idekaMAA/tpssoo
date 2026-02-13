// ============================================================================
// WORKER - instrucciones.h
// PASO A PASO GENERAL
// 1) Define el dispatcher de instrucciones de la Query
// 2) Declara funciones por operación (CREATE/TAG/TRUNCATE/WRITE/READ/.../END)
// 3) Integra memoria interna, Storage y Master
// ============================================================================

#ifndef INSTRUCCIONES_H
#define INSTRUCCIONES_H

#include <stdint.h>
#include <commons/log.h>
#include "instrucciones_parser.h"
#include "../../../utils/src/proto.h"

// ---------------------------------------------------------------------------
// Dispatcher principal
// ---------------------------------------------------------------------------
int ejecutar_instruccion(
    int           query_id,
    instruccion_t* inst,
    t_log*        logger,
    int           fd_storage,
    int           fd_master,
    uint32_t      retardo_ms,
    int           pc_actual
);

// ---------------------------------------------------------------------------
// Funciones por operación
// ---------------------------------------------------------------------------
int instr_create (int query_id, file_tag_t ft, t_log* logger, int fd_storage);
int instr_tag    (int query_id, file_tag_t ft_origen, file_tag_t ft_destino, t_log* logger, int fd_storage);
int instr_truncate(int query_id, instruccion_t* inst, file_tag_t ft, t_log* logger, int fd_storage);
int instr_write  (int query_id, instruccion_t* inst, file_tag_t ft, t_log* logger, int fd_storage, uint32_t retardo_ms);
int instr_read   (int query_id, instruccion_t* inst, file_tag_t ft, t_log* logger, int fd_storage, int fd_master, uint32_t retardo_ms);
int instr_commit (int query_id, file_tag_t ft, t_log* logger, int fd_storage);
int instr_flush  (int query_id, file_tag_t ft, t_log* logger, int fd_storage);
int instr_delete (int query_id, file_tag_t ft, t_log* logger, int fd_storage);
int instr_end(int query_id, t_log* logger, int fd_master, int final_pc, t_query_resultado estado);

#endif // INSTRUCCIONES_H
