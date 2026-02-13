// ============================================================================
// WORKER - mensajes_worker.h
// PASO A PASO GENERAL
// Este header define todos los mensajes que intercambia el Worker con:
// 1) Master  -> Worker  : Asignaciones de Query
// 2) Worker  -> Master  : Resultados y lecturas
// 3) Worker  -> Storage : Commit y operaciones sobre bloques
// ============================================================================

#ifndef MENSAJES_WORKER_H
#define MENSAJES_WORKER_H

#include <stdint.h>
#include "query_interpreter/instrucciones_parser.h"   // file_tag_t
#include "../../utils/src/proto.h"                    // opcodes, t_query_resultado

// ---------------------------------------------------------------------------
// 1) Master -> Worker : Solicitud de ejecutar una Query
//    Incluye:
//    - ID de Query
//    - nombre del archivo de operaciones
//    - PC inicial desde donde comenzar la ejecución
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t query_id;
    char     filename[256];
    uint32_t pc_inicial;
} t_exec_query;

// ---------------------------------------------------------------------------
// 2) Worker -> Master : Finalización de una Query
//    Informa al Master:
//    - Query ejecutada
//    - PC final alcanzado
//    - Estado final (OK / ERROR / CANCELADA)
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t query_id;
    uint32_t final_pc;
    t_query_resultado estado;
} t_query_end;

// ---------------------------------------------------------------------------
// 3) Worker -> Master : Resultado de una instrucción READ
//    Incluye:
//    - Query que hizo el READ
//    - Tamaño de contenido leído
//    - Buffer con los datos leídos
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t query_id;
    uint32_t tamanio_contenido;
    char     contenido[512];
} t_read_result;

// ---------------------------------------------------------------------------
// 4) Worker -> Storage : Solicitud de COMMIT de un Archivo (FILE,TAG)
//    Utilizado cuando se ejecuta la instrucción COMMIT
// ---------------------------------------------------------------------------
typedef struct {
    file_tag_t ft;
} w2s_commit_struct_t;

// ---------------------------------------------------------------------------
// 5) Worker -> Storage : Operación de IO sobre un bloque
//    Utilizado para:
//    - READ BLOCK
//    - WRITE BLOCK
// ---------------------------------------------------------------------------
typedef struct {
    file_tag_t ft;
    uint32_t   block_number;
} w2s_block_io_struct_t;

#endif // MENSAJES_WORKER_H
