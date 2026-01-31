#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include "paquete.h"

#define M_MAX_PATH 4096

typedef uint16_t op_code;

// =================== OPCODES ===================

enum {
    OP_SUBMIT_QUERY   = 1,   // Query Control -> Master
    OP_GET_BLOCK_SIZE = 2,   // Worker -> Storage
    OP_BLOCK_SIZE     = 3,   // Storage -> Worker
    OP_HELLO_WORKER   = 4,   // Worker -> Master (Handshake)

    // Master -> Worker 
    OP_ASIGNACION_QUERY          = 5,   // Envía una Query al Worker
    OP_DESALOJO_QUERY            = 6,   // Desalojo por prioridad
    OP_DESALOJO_POR_CANCELACION  = 7,   // Desalojo por desconexión del QC
    
    // Worker -> Master
    OP_QUERY_END              = 8,   // Fin de Query (id, pc_final, estado)
    OP_READ_RESULT            = 9,   // Resultado de READ (id, cstring)
    OP_DESALOJO_PRIORIDAD_OK  = 10,  // ACK desalojo por prioridad (query_id, pc_actual)
    OP_DESALOJO_CANCELACION_OK = 18, // ACK desalojo por cancelación (query_id, pc_actual)

    // Worker -> Storage
    OP_COMMIT      = 11,
    OP_WRITE_BLOCK = 12,
    OP_READ_BLOCK  = 13,
    OP_TRUNCATE    = 14,
    OP_TAG         = 15,
    OP_DELETE      = 16,
    OP_CREATE      = 17,

    // Respuestas genéricas
    OP_OK          = 100,
    OP_ERROR       = 101,
    OP_BLOCK_DATA  = 102
};

// Alias para no romper código viejo que use OP_DESALOJO_OK
#define OP_DESALOJO_OK OP_DESALOJO_PRIORIDAD_OK

// (Opcional, por si te ayuda a no romper compilación mientras migrás)
// #define OP_DESALOJO_OK OP_DESALOJO_PRIORIDAD_OK

// =================== ESTADOS DE QUERY ===================
typedef enum {
    QUERY_OK = 0,
    QUERY_ERROR = 1,
    QUERY_CANCELADA = 2
} t_query_resultado;

// =================== ESTRUCTURAS DE PROTOCOLO ===================
typedef struct {
    uint32_t prioridad;
    char     path[M_MAX_PATH];
} t_submit_query;

typedef struct __attribute__((__packed__)) {
    uint16_t opcode;
    uint32_t len;
} t_frame_hdr;

typedef struct {
    uint32_t worker_id;
} t_hello_worker;

// =================== API DE FRAMING ===================
int enviar_paquete(int fd, uint16_t op_code, const t_paquete* paquete);
int recibir_paquete(int fd, uint16_t* op_code, t_paquete* paquete);

#endif