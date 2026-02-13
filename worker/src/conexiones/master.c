#include "master.h"
#include "../../utils/src/net.h"
#include "../../utils/src/paquete.h"
#include "../../utils/src/proto.h"
#include <unistd.h>
#include <stdio.h>

int enviar_hello_worker(const char* ip_master, int puerto_master, t_log* logger, uint32_t worker_id) {
    char pstr[16];
    snprintf(pstr, sizeof(pstr), "%d", puerto_master);

    int fd_master = conectar_a(ip_master, pstr);
    if (fd_master < 0) {
        if (logger)
            log_error(logger, "No pude conectar a Master %s:%s", ip_master, pstr);
        return -1;
    }

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_uint32(&p, worker_id) != 0) {
        if (logger) log_error(logger, "No pude empaquetar HELLO_WORKER");
        paquete_destruir(&p);
        close(fd_master);
        return -1;
    }

    if (enviar_paquete(fd_master, OP_HELLO_WORKER, &p) != 0) {
        if (logger) log_error(logger, "Error enviando HELLO_WORKER al Master");
        paquete_destruir(&p);
        close(fd_master);
        return -1;
    }

    paquete_destruir(&p);

    if (logger)
        log_info(logger, "HELLO_WORKER enviado a Master (worker_id=%u)", worker_id);

    return fd_master; // fd listo para usar
}

/**
 * Enviar resultado de READ al Master.
 * payload = [u32 query_id][cstring resultado]
 */
int enviar_resultado_a_master(int fd_master, int query_id, const char* result_data, t_log* logger) {
    if (fd_master < 0) return -1;

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_uint32(&p, (uint32_t)query_id) != 0 ||
        paquete_cargar_cstring(&p, result_data ? result_data : "") != 0) {

        if (logger) log_error(logger, "[MASTER] No pude empaquetar READ_RESULT");
        paquete_destruir(&p);
        return -1;
    }

    int rc = enviar_paquete(fd_master, OP_READ_RESULT, &p);
    paquete_destruir(&p);

    if (rc != 0 && logger) {
        log_error(logger, "[MASTER] Error enviando READ_RESULT (Q=%d)", query_id);
    } else if (logger) {
        log_info(logger, "[MASTER] READ_RESULT enviado (Q=%d, len=%u)", query_id, p.buffer.size);
    }

    return rc;
}

/**
 * Enviar fin de Query al Master.
 * payload = [u32 query_id][u32 pc_final][u32 estado]
 */
int enviar_end_a_master(int fd_master,
                        uint32_t query_id,
                        uint32_t pc_final,
                        t_query_resultado estado,
                        t_log* logger) {
    if (fd_master < 0) return -1;

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_uint32(&p, query_id) != 0 ||
        paquete_cargar_uint32(&p, pc_final) != 0 ||
        paquete_cargar_uint32(&p, (uint32_t)estado) != 0) {

        if (logger) log_error(logger, "[MASTER] No pude empaquetar QUERY_END");
        paquete_destruir(&p);
        return -1;
    }

    int rc = enviar_paquete(fd_master, OP_QUERY_END, &p);
    paquete_destruir(&p);

    if (rc != 0 && logger) {
        log_error(logger, "[MASTER] Error enviando QUERY_END (Q=%u)", query_id);
    } else if (logger) {
        log_info(logger, "[MASTER] QUERY_END enviado (Q=%u, pc=%u, estado=%d)",
                 query_id, pc_final, estado);
    }

    return rc;
}
// ACK genérico de desalojo (compatibilidad, lo usamos para prioridad)
int master_enviar_desalojo_ok(int fd_master, uint32_t query_id, uint32_t pc_actual) {
    t_paquete p;
    paquete_iniciar(&p);

    // payload: [u32 query_id][u32 pc_actual]
    paquete_cargar_uint32(&p, query_id);
    paquete_cargar_uint32(&p, pc_actual);

    int rc = enviar_paquete(fd_master, OP_DESALOJO_PRIORIDAD_OK, &p);
    paquete_destruir(&p);
    return rc;
}

// Desalojo por prioridad (reencolar en READY)
int master_enviar_desalojo_prioridad_ok(int fd_master, uint32_t query_id, uint32_t pc_actual) {
    t_paquete p;
    paquete_iniciar(&p);

    paquete_cargar_uint32(&p, query_id);
    paquete_cargar_uint32(&p, pc_actual);

    int rc = enviar_paquete(fd_master, OP_DESALOJO_PRIORIDAD_OK, &p);
    paquete_destruir(&p);
    return rc;
}

// Desalojo por cancelación / desconexión del QC
int master_enviar_desalojo_cancelacion_ok(int fd_master, uint32_t query_id, uint32_t pc_actual) {
    t_paquete p;
    paquete_iniciar(&p);

    paquete_cargar_uint32(&p, query_id);
    paquete_cargar_uint32(&p, pc_actual);

    int rc = enviar_paquete(fd_master, OP_DESALOJO_CANCELACION_OK, &p);
    paquete_destruir(&p);
    return rc;
}