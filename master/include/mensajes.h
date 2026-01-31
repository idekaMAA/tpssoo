#ifndef MASTER_MENSAJES_H
#define MASTER_MENSAJES_H

#include <stdint.h>
#include "../../utils/src/proto.h"  

// Struct para envío de Query a Worker
typedef struct {
    uint32_t query_id;
    char filename[256];
    uint32_t pc_inicial;
} t_exec_query;

// Struct para desalojar una query por Prioridad
typedef struct {
    int query_id;
} t_desalojo_query;

// Lectura recibida por el Worker
typedef struct {
    uint32_t query_id;
    uint32_t tamanio_contenido;
    char contenido[512]; // Buffer para el contenido
} t_read_result;

// Fin de una Query recibido por el Worker
typedef struct {
    uint32_t query_id;
    uint32_t final_pc;          // Program Counter final
    t_query_resultado estado;   // Estado final: OK, ERROR o CANCELADA
} t_query_end;

#endif // MASTER_MENSAJES_H
