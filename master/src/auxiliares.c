#include "../include/auxiliares.h"
#include "../include/inicializaciones.h"

t_query* _query_a_buscar = NULL;

// SERVIDOR)
t_query* buscar_query_por_id(uint32_t id) {
    t_query* q = NULL;

    // 1. Buscamos en EXEC 
    pthread_mutex_lock(&mutex_exec);
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_query* aux = list_get(cola_exec, i);
        if (aux->id == id) {
            q = aux;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_exec);

    
    if (q != NULL) return q;

    
    pthread_mutex_lock(&mutex_ready);
    for (int i = 0; i < list_size(cola_ready); i++) {
        t_query* aux = list_get(cola_ready, i);
        if (aux->id == id) {
            q = aux;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_ready);

    return q; 
}


// (SERVIDOR)
t_worker* buscar_worker_por_fd(int fd) {
    pthread_mutex_lock(&mutex_workers);

    for (int i = 0; i < list_size(workers); i++) {
        t_worker* w = list_get(workers, i);
        if (w->fd == fd) {
            pthread_mutex_unlock(&mutex_workers);
            return w;
        }
    }

    pthread_mutex_unlock(&mutex_workers);
    return NULL;
}

//(PLANIFICADOR)
bool worker_esta_libre(void* w_void) {
    t_worker* w = (t_worker*) w_void;
    return w->libre;
}

// (PLANIFICADOR)
t_query* obtener_query_menor_prioridad() {
    pthread_mutex_lock(&mutex_exec);
    if (list_is_empty(cola_exec)) {
        pthread_mutex_unlock(&mutex_exec);
        return NULL;
    }

    t_query* q_menor = list_get(cola_exec, 0);
    for (int i = 1; i < list_size(cola_exec); i++) {
        t_query* q = list_get(cola_exec, i);
        if (q->prioridad > q_menor->prioridad) {
            q_menor = q;
        }
    }

    pthread_mutex_unlock(&mutex_exec);
    return q_menor;
}

// PLANIFICADOR)
bool worker_tiene_query(void* w_void) {
    t_worker* w = (t_worker*) w_void;
    return w->query_actual == _query_a_buscar;
}

// (COLA READY)
bool comparar_prioridad(void* elem1, void* elem2) {
    t_query* q1 = (t_query*) elem1;
    t_query* q2 = (t_query*) elem2;
    return q1->prioridad < q2->prioridad;
}