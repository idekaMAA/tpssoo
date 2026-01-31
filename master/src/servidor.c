#include "../include/servidor.h"
#include "../include/inicializaciones.h"
#include "../include/cola_ready.h"
#include "../include/cliente.h"
#include "../include/auxiliares.h"
#include "../../utils/src/proto.h"
#include "../../utils/src/paquete.h"
#include "../../utils/src/net.h"
#include "../include/aging.h"
#include <string.h>
#include <errno.h>

void* atender_cliente(void* arg) {

    int cfd = *(int*) arg;
    free(arg);

    while (1) {
        uint16_t op = 0;
        t_paquete paq;
        memset(&paq, 0, sizeof(t_paquete));

        int res = recibir_paquete(cfd, &op, &paq);

        if (res != 0) {
    
            t_worker* w = buscar_worker_por_fd(cfd);
        
            if (w) {
                log_debug(logger, "## Se desconectó el Worker %d", w->worker_id);
                manejar_desconexion_worker(w); 
            } else {
                log_debug(logger, "## Se desconectó un Query Control (fd=%d)", cfd);
                manejar_desconexion_qc(cfd); 
            }

            close(cfd);
            break; 
        }

        switch (op) {
            case OP_SUBMIT_QUERY: {
                if (paq.buffer.size < sizeof(uint32_t) + 2) { 
                    log_error(logger, "Tamaño inválido SUBMIT_QUERY: %u", paq.buffer.size);
                    paquete_destruir(&paq);
                    close(cfd);
                    return NULL;
                }

                uint32_t prioridad;
                memcpy(&prioridad, paq.buffer.stream, sizeof(uint32_t));

                char* path = (char*)paq.buffer.stream + sizeof(uint32_t);

                t_query* q = malloc(sizeof(t_query));
                q->id = __sync_fetch_and_add(&id_counter, 1);
                q->path_query = strdup(path);
                q->estado = READY;
                q->fd_query_control = cfd;
                q->fd_worker_asignado = -1;
                q->prioridad = prioridad;
                q->program_counter = 0;
                q->payload = NULL;
                q->tiempo_entrada_ready = 0; 
            
                q->tiempo_entrada_ready = obtener_timestamp_ms();

                log_info(logger, "## Se conecta un Query Control para ejecutar la Query %s con prioridad %u - Id asignado: %d. Nivel de multiprocesamiento %d", 
                        q->path_query, q->prioridad, q->id, list_size(workers)); // OBLIGATORIO
 
                ready_push(q);

                paquete_destruir(&paq);
                break;
            }

            case OP_HELLO_WORKER: {
                if (paq.buffer.size != sizeof(uint32_t)) {
                    log_error(logger, "Tamaño inválido HELLO_WORKER: %u", paq.buffer.size);
                    paquete_destruir(&paq); 
                    close(cfd); 
                    return NULL;
                }

                uint32_t worker_id;
                memcpy(&worker_id, paq.buffer.stream, sizeof(worker_id));
                paquete_destruir(&paq);

                t_worker* w = malloc(sizeof(t_worker));
                w->worker_id = worker_id;
                w->fd = cfd; 
                w->libre = true;
                w->query_actual = NULL;

                pthread_mutex_lock(&mutex_workers);
                list_add(workers, w);
                pthread_mutex_unlock(&mutex_workers);

                sem_post(&sem_workers); 

                log_info(logger, "## Se conecta el Worker %d - Cantidad total de Workers: %d", w->worker_id, list_size(workers)); // OBLIGATORIO
                break;
            }

            case OP_READ_RESULT: {
                uint32_t offset = 0;

                uint32_t query_id;

                memcpy(&query_id, paq.buffer.stream + offset, sizeof(uint32_t));
                offset += sizeof(uint32_t);

                const char* contenido = (char*)(paq.buffer.stream + offset);

                log_debug(logger, "Llega READ_RESULT de Worker. QueryID=%u Contenido=\"%s\"", query_id, contenido);

        
                t_query* q = buscar_query_por_id(query_id);
            
                if (!q) {
                    log_error(logger, "No se encontró query %u para READ_RESULT (ni en Exec ni en Ready)", query_id);
                    paquete_destruir(&paq);
                    break;
                }

                // (Tolerancia sin romper) Se puede mandar la lectura al QC si se lo necesita 
                if (q->estado == READY) {
                    log_warning(logger, "Ignorando READ_RESULT de Query %u (encontrada en READY, fue desalojada).", query_id);
                    paquete_destruir(&paq);
                    break;
                }

                t_worker* w = buscar_worker_por_fd(q->fd_worker_asignado);
                if (!w) {
                    log_error(logger, "No se encontró Worker asociado (fd=%d) para Query %u",
                            q->fd_worker_asignado, q->id);
                    paquete_destruir(&paq);
                    break;
                }
                
                if (q->payload) free(q->payload);
                q->payload = strdup(contenido);
            
                enviar_read_a_query_control(q, w); 

                paquete_destruir(&paq);
                break;
            }
            
            case OP_QUERY_END: {
    
                if (paq.buffer.size != sizeof(t_query_end)) {
                    log_error(logger, "Tamaño inválido para OP_QUERY_END: %u", paq.buffer.size);
                    paquete_destruir(&paq);
                    break;
                }

                t_query_end* q_end = (t_query_end*) paq.buffer.stream;

                uint32_t query_id = q_end->query_id;
                uint32_t final_pc = q_end->final_pc;
                t_query_resultado final_status = q_end->estado; 

                log_debug(logger, "Llega QUERY_END de Worker. QueryID=%u PC_final=%u Estado=%u",
                        query_id, final_pc, (unsigned)final_status);

                
                t_query* q = buscar_query_por_id(query_id);

               if (q) {
                    q->program_counter = final_pc;
                
                    if (q->estado == EXEC) {
                        pthread_mutex_lock(&mutex_exec);
                        list_remove_element(cola_exec, q);
                        pthread_mutex_unlock(&mutex_exec);
                    } 
                    else if (q->estado == READY) {
                        // Caso raro
                        pthread_mutex_lock(&mutex_ready);
                        list_remove_element(cola_ready, q);
                        pthread_mutex_unlock(&mutex_ready);
                    }
                    
                    q->estado = EXIT;
                } else {
                    log_error(logger, "No se encontró query %u para QUERY_END", query_id);
                }

                int fd_usado = (q) ? q->fd_worker_asignado : cfd;
                t_worker* w = buscar_worker_por_fd(fd_usado);

                if (w) {
                    pthread_mutex_lock(&mutex_workers);
                    w->libre = true;
                    w->query_actual = NULL;
                    pthread_mutex_unlock(&mutex_workers);
            
                    log_info(logger, "## Se terminó la Query %u en el Worker %u", q->id, w->worker_id); // OBLIGATORIO
                } else {
                    log_warning(logger, "No se encontró Worker asociado (fd=%d) para Query %u",
                            fd_usado, q->id);
                }   
 
                enviar_end_a_query_control(q->fd_query_control, q->id, final_status);
         
                sem_post(&sem_workers); 

                paquete_destruir(&paq);
                break;
            }

            case OP_DESALOJO_PRIORIDAD_OK: { // Por prioridad
                uint32_t query_id;
                uint32_t pc_actual;

                memcpy(&query_id, paq.buffer.stream, sizeof(uint32_t));
                memcpy(&pc_actual, paq.buffer.stream + sizeof(uint32_t), sizeof(uint32_t));

                log_debug(logger, "## Worker devuelve contexto por desalojo. QueryID=%u PC=%u",
                    query_id, pc_actual);

                t_query* q = buscar_query_por_id(query_id);
            
                if (q) {
                    q->program_counter = pc_actual;
                } else {
                    log_error(logger, "No se encontró query %u para actualizar PC en desalojo", query_id);
                }

                t_worker* w = NULL;
                w = buscar_worker_por_fd(cfd);

                if (w) {
                    pthread_mutex_lock(&mutex_workers);
                    w->libre = true;
                    w->query_actual = NULL;
                    pthread_mutex_unlock(&mutex_workers);
                }


                if (q) {
                    log_info(logger, "## Se desaloja la Query %u (%u) del Worker %d - Motivo: PRIORIDAD", 
                    q->id, q->prioridad, w->worker_id); // OBLIGATORIO
                } else {
                    log_info(logger, "## Se desaloja la Query %u (?) del Worker %d - Motivo: PRIORIDAD", 
                            query_id, w->worker_id); // OBLIGATORIO
                }   

                sem_post(&sem_workers);
                paquete_destruir(&paq);
                break;
            }


            case OP_DESALOJO_CANCELACION_OK: { // Por desconexion de QC
                uint32_t query_id;
                uint32_t pc_actual;

                memcpy(&query_id, paq.buffer.stream, sizeof(uint32_t));
                memcpy(&pc_actual, paq.buffer.stream + sizeof(uint32_t), sizeof(uint32_t));

                log_debug(logger,
                    "## Worker devuelve contexto por cancelación. QueryID=%u PC=%u",
                    query_id, pc_actual);

               
                t_query* q = buscar_query_por_id(query_id);
        

                if (!q) {
                    log_error(logger, "No se encontró la query %u para OP_DESALOJO_CANCELACION_OK", query_id);
                    paquete_destruir(&paq);
                   // break;
                }

                q->program_counter = pc_actual; // Guardamos el PC aunque aunque la query no se use mas por formalidad..

                pthread_mutex_lock(&mutex_exec);
                list_remove_element(cola_exec, q);
                pthread_mutex_unlock(&mutex_exec);
            
                t_worker* w = buscar_worker_por_fd(cfd);

                if (w) {
                    pthread_mutex_lock(&mutex_workers);
                    w->libre = true;
                    w->query_actual = NULL; 
                    pthread_mutex_unlock(&mutex_workers);
                }

                q->estado = EXIT;
                q->fd_worker_asignado = -1;
                q->fd_query_control = -1;

                if (q) {
                    log_info(logger, "## Se desaloja la Query %u (%u) del Worker %d - Motivo: DESCONEXION", 
                            q->id, q->prioridad, w->worker_id); // OBLIGATORIO
                } else {
                    log_info(logger, "## Se desaloja la Query %u (?) del Worker %d - Motivo: DESCONEXION", 
                            query_id, w->worker_id); // OBLIGATORIO
                }


                log_info(logger,"## Se desconecta un Query Control. Se finaliza la Query %u con prioridad %d . Nivel multiprocesamiento %d",
                         q->id, q->prioridad, list_size(workers)); // OBLIGATORIO

                sem_post(&sem_workers);
                paquete_destruir(&paq);  
                break;
            }

            default:
                log_warning(logger, "Opcode desconocido: %u", op);
                paquete_destruir(&paq);
                close(cfd);
                break;
        }
    }

    return NULL;
}

void manejar_desconexion_worker(t_worker* w) {
    pthread_mutex_lock(&mutex_exec);

    t_query* q = NULL;
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_query* aux = list_get(cola_exec, i);
        if (aux->fd_worker_asignado == w->fd) {
            q = aux;
            break;
        }
    }

    if (q) {
        log_info(logger,"## Se desconecta el Worker %d - Se finaliza la Query %u - Cantidad total de Workers: %d",
                    w->worker_id, q->id, list_size(workers) - 1 ); // OBLIGATORIO

        q->estado = EXIT;
        q->fd_worker_asignado = -1;

        // Notificamos al QC si hay
        if (q->fd_query_control != -1) {
            enviar_end_a_query_control(q->fd_query_control, q->id, QUERY_ERROR);

            log_debug(logger, "## Notificado QC %d: Worker %d desconectado. Query %u finalizada con error",
                     q->fd_query_control, w->worker_id, q->id);

        } else {
            log_debug(logger, "## Worker %d desconectado. Query %u finalizada con error (QC ya desconectado)",
                     w->worker_id, q->id);
        }
    } else {
        log_info(logger, "## Worker %d (fd=%d) se desconectó sin tener Query en ejecución",
                 w->worker_id, w->fd);
    }

    pthread_mutex_unlock(&mutex_exec);

    pthread_mutex_lock(&mutex_workers);
    list_remove_element(workers, w);
    pthread_mutex_unlock(&mutex_workers);

    free(w);

    log_debug(logger, "## Worker desconectado eliminado del sistema. Total de Workers: %d", list_size(workers));
}

void manejar_desconexion_qc(int cfd) {

    // READY
    pthread_mutex_lock(&mutex_ready);

    for (int i = 0; i < list_size(cola_ready); ) {
        t_query* q = (t_query*) list_get(cola_ready, i);
        if (!q) { 
            i++;
            continue;
        }

        if (q->fd_query_control == cfd) {
        
            t_query* removida = (t_query*) list_remove(cola_ready, i);
            removida->estado = EXIT;
            removida->fd_query_control = -1;

            removida->fd_worker_asignado = -1;
         
            log_info(logger, "## Se desconecta un Query Control. Se finaliza la Query %u con prioridad %d . Nivel multiprocesamiento %d",
                     removida->id, removida->prioridad, list_size(workers)); // OBLIGATORIO

        } else {
            i++;
        }
    }

    pthread_mutex_unlock(&mutex_ready);

    // EXEC
    pthread_mutex_lock(&mutex_exec);

    for (int i = 0; i < list_size(cola_exec);) {
        t_query* q = (t_query*) list_get(cola_exec, i);

        if (!q) { 
            i++; 
            continue;
        }

        if (q->fd_query_control == cfd) {

            t_worker* w = buscar_worker_por_fd(q->fd_worker_asignado);
            
            if (w) {
                t_desalojo_query desalojo;
                desalojo.query_id = q->id;

                log_info(logger, "## QC desconectado: notificar al Worker %d que desaloje la Query %d",
                        w->worker_id, q->id);

                int res = enviar_desalojo_por_desconexion_fd(w->fd, &desalojo);
                if (res != 0) {
                    log_error(logger, "No se pudo enviar desalojo de Query %d al Worker %d", q->id, w->worker_id);
                }
                i++;

            } else {
                log_warning(logger, "Query %d estaba en EXEC pero no tiene Worker asignado, movida a EXIT", q->id);
                q->estado = EXIT; 
                
                list_remove_element(cola_exec, q);
                
            }
        } else {
            i++;
        }
    }

    pthread_mutex_unlock(&mutex_exec);

}
