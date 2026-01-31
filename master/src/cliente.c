#include "../include/cliente.h"
#include "../include/inicializaciones.h"

// Serializacion y envio a worker de una query a ejecutar
int enviar_asignacion_query_fd(int fd_worker, const t_exec_query* asignacion) {
    if (fd_worker < 0 || !asignacion) return -1;

    t_paquete paq;
    paquete_iniciar(&paq);

    
    if (paquete_cargar_uint32(&paq, asignacion->query_id) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    
    if (paquete_cargar_cstring(&paq, asignacion->filename) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

   
    if (paquete_cargar_uint32(&paq, asignacion->pc_inicial) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    
    if (enviar_paquete(fd_worker, OP_ASIGNACION_QUERY, &paq) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    paquete_destruir(&paq);
    return 0;
}

// Desalojo de una query por prioridad
int enviar_desalojo_por_prioridad_fd(int fd_worker, const t_desalojo_query* desalojo) {
    if (fd_worker < 0 || !desalojo) return -1;

    t_paquete paq;
    paquete_iniciar(&paq);

    
    if (paquete_cargar_uint32(&paq, desalojo->query_id) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    
    if (enviar_paquete(fd_worker, OP_DESALOJO_QUERY, &paq) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    paquete_destruir(&paq);
    return 0;
}

// Envio de lectura a un QC
int enviar_read_a_query_control(t_query* q, t_worker* w) {
    if (!q || !w) return -1;
    
    t_paquete paq;
    paquete_iniciar(&paq);

    
    if (paquete_cargar_cstring(&paq, q->payload) != 0) {
        log_error(logger, "Error cargando payload al paquete para QC");
        paquete_destruir(&paq);
        return -1;
    }

    
    if (enviar_paquete(q->fd_query_control, OP_READ_RESULT, &paq) != 0) {
        log_error(logger, "Error enviando OP_READ_RESULT al Query Control (fd=%d)", q->fd_query_control);
        paquete_destruir(&paq);
        return -1;
    }

    log_info(logger, "## Se envía un mensaje de lectura de la Query %u en el Worker %d al Query Control", q->id, w->worker_id ); // OBLIGATORIO
   
    paquete_destruir(&paq);
    return 0;
}

// Finalizcion de una query, y su motivo
int enviar_end_a_query_control(int fd_query_control, uint32_t query_id, t_query_resultado final_status) {
    if (fd_query_control < 0) return -1;

    t_paquete paq;
    paquete_iniciar(&paq);

    const char* motivo;
    switch (final_status) {
        case QUERY_OK:
            motivo = "Finalización exitosa de la Query";
            break;
        case QUERY_ERROR:
            motivo = "Finalización con error en Worker";
            break;
        case QUERY_CANCELADA:
            motivo = "Query cancelada o desconectada";
            break;
        default:
            motivo = "Motivo desconocido";
            break;
    }

    if (paquete_cargar_cstring(&paq, motivo) != 0) {
        log_error(logger, "Error cargando motivo en paquete de fin de Query");
        paquete_destruir(&paq);
        return -1;
    }

    log_info(logger, "## Se notifica fin de la Query %u al Query Control (fd=%d): %s",
            query_id, fd_query_control, motivo); 
            
    if (enviar_paquete(fd_query_control, OP_QUERY_END, &paq) != 0) {
        log_error(logger, "Error enviando OP_QUERY_END al Query Control (fd=%d)", fd_query_control);
        paquete_destruir(&paq);
        return -1;
    }

    paquete_destruir(&paq);
    return 0;
}

// Cancelacion de una Query por desconexion del QC (misma struct que desalojo, diferente op code)
int enviar_desalojo_por_desconexion_fd(int fd_worker, const t_desalojo_query* desalojo){
    if (fd_worker < 0 || !desalojo) return -1;

    t_paquete paq;
    paquete_iniciar(&paq);

    
    if (paquete_cargar_uint32(&paq, desalojo->query_id) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    
    if (enviar_paquete(fd_worker, OP_DESALOJO_POR_CANCELACION, &paq) != 0) {
        paquete_destruir(&paq);
        return -1;
    }

    paquete_destruir(&paq);
    return 0;
}