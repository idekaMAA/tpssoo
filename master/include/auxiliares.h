#ifndef AUXILIARES_H_
#define AUXILIARES_H_

#include "main.h"

// ----- VARIABLES -----
extern t_query* _query_a_buscar;

// ----- FUNCIONES -----
t_query* buscar_query_por_id(uint32_t id);
t_worker* buscar_worker_por_fd(int fd);
bool worker_esta_libre(void* w_void);
t_query* obtener_query_menor_prioridad();
bool worker_tiene_query(void* w_void);  
bool comparar_prioridad(void* elem1, void* elem2);


#endif