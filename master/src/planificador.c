#include "../include/planificador.h"
#include "../include/cola_ready.h"
#include "../include/inicializaciones.h"
#include "../include/cliente.h"
#include "../include/auxiliares.h"
#include "../include/aging.h"
#include <errno.h>

void* planificador_loop(void* arg) {

    while (1) {

        sem_wait(&sem_ready);
        t_query* q = ready_pop();

        if (q == NULL) {
            continue; 
        }

        bool tengo_worker_libre = false;
        
        if (sem_trywait(&sem_workers) == 0) {
            tengo_worker_libre = true;
        } else {
            // Verificacion del desalojo
            if (strcmp(config_master.algoritmo_planificacion, "PRIORIDADES") == 0) { 
                
                t_query* q_menor = obtener_query_menor_prioridad();

                if (q_menor != NULL && q_menor->prioridad > q->prioridad) {
                    
                    log_debug(logger, "## Intentando desalojar Query %d (Prio %d) para ejecutar Query %d (Prio %d)", 
                             q_menor->id, q_menor->prioridad, q->id, q->prioridad);

                    _query_a_buscar = q_menor;
                    pthread_mutex_lock(&mutex_workers);
                    t_worker* w_objetivo = list_find(workers, worker_tiene_query);
                    pthread_mutex_unlock(&mutex_workers);

                    if (w_objetivo != NULL) {
                        t_desalojo_query desalojo = {0};
                        desalojo.query_id = q_menor->id;

                        if (enviar_desalojo_por_prioridad_fd(w_objetivo->fd, &desalojo) == 0) {
                            
                            pthread_mutex_lock(&mutex_exec);
                            list_remove_element(cola_exec, q_menor);
                            pthread_mutex_unlock(&mutex_exec);

                            q_menor->estado = READY;
                            q_menor->fd_worker_asignado = -1;
                            q->tiempo_entrada_ready = obtener_timestamp_ms();
                            ready_push(q_menor); 

                            sem_wait(&sem_workers); // Esperamos sem post del worker en servidor.c
                            
                            tengo_worker_libre = true;
                            
                            log_info(logger, "## Desalojo confirmado. Asignando Query %d al Worker %d", q->id, w_objetivo->worker_id);
                        }
                    }
                }
            }
        }

        if (!tengo_worker_libre) {
            ready_push(q); 
            
            if (strcmp(config_master.algoritmo_planificacion, "FIFO") == 0) {
                sem_wait(&sem_workers); 
                sem_post(&sem_workers); 
            } 
            else {
                usleep(100000); 
            }
            
            continue;
        }

        pthread_mutex_lock(&mutex_workers);
        t_worker* w = list_find(workers, (void*) worker_esta_libre);
        
        if (w == NULL) {
            pthread_mutex_unlock(&mutex_workers);
            ready_push(q);
            sem_post(&sem_workers); 
            continue;
        }

        w->libre = false;
        pthread_mutex_unlock(&mutex_workers);

        t_exec_query asignacion = {0};
        asignacion.query_id = q->id;
        strncpy(asignacion.filename, q->path_query, sizeof(asignacion.filename) - 1);
        asignacion.pc_inicial = q->program_counter;

        if (enviar_asignacion_query_fd(w->fd, &asignacion) != 0) {
            log_error(logger, "Error al enviar Query %d al Worker %d", q->id, w->worker_id);
            
            pthread_mutex_lock(&mutex_workers);
            w->libre = true;
            pthread_mutex_unlock(&mutex_workers);
            
            sem_post(&sem_workers);
            ready_push(q);
            continue;
        }

        pthread_mutex_lock(&mutex_workers);
        w->query_actual = q;
        pthread_mutex_unlock(&mutex_workers);

        q->fd_worker_asignado = w->fd;
        q->estado = EXEC;

        pthread_mutex_lock(&mutex_exec);
        list_add(cola_exec, q);
        pthread_mutex_unlock(&mutex_exec);

        log_info(logger, "## Se envía la Query %d (%d) al Worker %d", q->id, q->prioridad, w->worker_id); // OBLIGATORIO
    }
    return NULL;
}