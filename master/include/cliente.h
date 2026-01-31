#ifndef CLIENTE_H_
#define CLIENTE_H_

#include "main.h"

// ----- FUNCIONES -----
int enviar_asignacion_query_fd(int fd_worker, const t_exec_query* asignacion);
int enviar_desalojo_por_prioridad_fd(int fd_worker, const t_desalojo_query* desalojo);
int enviar_read_a_query_control(t_query* q, t_worker* w);
int enviar_end_a_query_control(int fd_query_control, uint32_t query_id, t_query_resultado final_status);
int enviar_desalojo_por_desconexion_fd(int fd_worker, const t_desalojo_query* desalojo);

#endif