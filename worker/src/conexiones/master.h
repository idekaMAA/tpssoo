#ifndef WORKER_MASTER_H
#define WORKER_MASTER_H

#include <stdint.h>
#include <commons/log.h>
#include "../../utils/src/proto.h"   // opcodes, t_query_resultado

// Abre conexión y hace handshake HELLO (devuelve fd o -1)
int enviar_hello_worker(const char* ip_master, int puerto_master, t_log* logger, uint32_t worker_id);

// Resultado de READ hacia Master
int enviar_resultado_a_master(int fd_master, int query_id, const char* result_data, t_log* logger);

// END de Query hacia Master
int enviar_end_a_master(int fd_master, uint32_t query_id, uint32_t pc_final, t_query_resultado estado, t_log* logger);

// ACK de desalojo hacia Master: payload = [u32 query_id][u32 pc_actual]
int master_enviar_desalojo_ok(int fd_master, uint32_t query_id, uint32_t pc_actual);

int master_enviar_desalojo_ok(int fd_master, uint32_t query_id, uint32_t pc_actual);
int master_enviar_desalojo_prioridad_ok(int fd_master, uint32_t query_id, uint32_t pc_actual);
int master_enviar_desalojo_cancelacion_ok(int fd_master, uint32_t query_id, uint32_t pc_actual);

#endif // WORKER_MASTER_H