
#include "../include/cola_ready.h"
#include "../include/inicializaciones.h"
#include "../include/auxiliares.h"
#include "../include/aging.h"

void ready_push(t_query* q) {
    pthread_mutex_lock(&mutex_ready);
    
    if (strcmp(config_master.algoritmo_planificacion, "FIFO") == 0) {
        list_add(cola_ready, q);  
        // log_debug(logger, "Query %d agregada a READY (FIFO)", q->id);
    } else if (strcmp(config_master.algoritmo_planificacion, "PRIORIDADES") == 0) {
        list_add_sorted(cola_ready, q, comparar_prioridad);
       //  log_debug(logger, "Query %d agregada a READY (PRIORIDAD)", q->id);
    } else {
        log_error(logger, "Algoritmo de planificación desconocido: %s", config_master.algoritmo_planificacion);
    }

    pthread_mutex_unlock(&mutex_ready);
    sem_post(&sem_ready);
}

t_query* ready_pop(void) {
    pthread_mutex_lock(&mutex_ready);

    if (list_is_empty(cola_ready)) {
        pthread_mutex_unlock(&mutex_ready);
        return NULL;
    }

    t_query* q = list_remove(cola_ready, 0);

    pthread_mutex_unlock(&mutex_ready);
    return q;
}
