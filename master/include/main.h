#ifndef MAIN_H_
#define MAIN_H_

#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/queue.h>
#include <commons/collections/list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdint.h>
#include <semaphore.h>
#include <stdbool.h>  // para bool

#include "mensajes.h"
#include "../../utils/src/net.h"
#include "../../utils/src/paquete.h"
#include "../../utils/src/proto.h"   // trae t_query_resultado y t_hello_worker (no redefinir aquí)

// ----- ENUMS -----
typedef enum {
    READY,
    EXEC,
    EXIT
} t_estado_query;

// ----- STRUCTS -----
typedef struct {
    uint32_t id;
    char* path_query;            // ruta al archivo de la query
    t_estado_query estado;
    int fd_query_control;        // socket de quien la envió (-1 si no aplica)
    int fd_worker_asignado;      // socket del worker ejecutándola (-1 si no aplica)
    int prioridad;
    uint32_t program_counter;    
    char* payload;               // lo que envió el query control (texto)
    uint64_t tiempo_entrada_ready; // Aging
} t_query;

typedef struct {
    int worker_id;        
    int fd;               // socket del worker
    bool libre;           // si puede recibir una query
    t_query* query_actual;// si está ejecutando una query
} t_worker;

// ----- FUNCIONES -----
void liberar_estructuras(void);
void destruir_estructuras(void);

#endif
