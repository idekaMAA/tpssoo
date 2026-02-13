#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "paquete.h"
#include "proto.h"
#include "net.h"

#include <commons/log.h>

#include "config.h"
#include "mensajes.h"
#include "inicializacion.h"
#include "fs_manager.h"
#include "storage_utils.h"

// var. globales
extern storage_config_t g_storage_config;

// ============================================================================
// CONTADOR GLOBAL DE WORKERS CONECTADOS (thread-safe)
// ============================================================================
static pthread_mutex_t g_workers_mx = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_workers_conectados = 0;

static void log_worker_conexion(uint32_t worker_id) {
    pthread_mutex_lock(&g_workers_mx);
    g_workers_conectados++;
    uint32_t cant = g_workers_conectados;
    pthread_mutex_unlock(&g_workers_mx);

    log_info(
        g_storage_config.logger,
        "##Se conecta el Worker %u - Cantidad de Workers: %u",
        worker_id,
        cant
    );
}

static void log_worker_desconexion(uint32_t worker_id) {
    pthread_mutex_lock(&g_workers_mx);
    if (g_workers_conectados > 0) g_workers_conectados--;
    uint32_t cant = g_workers_conectados;
    pthread_mutex_unlock(&g_workers_mx);

    log_warning(
        g_storage_config.logger,
        "##Se desconecta el Worker %u - Cantidad de Workers: %u",
        worker_id,
        cant
    );
}

// hilo para atender workers
void* atender_worker(void* socket_ptr) {
    int fd_worker = *((int*)socket_ptr);
    free(socket_ptr);

    bool conectado = true;

    // ------------------------------------------------------------------------
    // Identidad del worker (la aprendemos en el handshake OP_GET_BLOCK_SIZE)
    // ------------------------------------------------------------------------
    uint32_t worker_id = 0;
    bool worker_registrado = false;

    log_info(
        g_storage_config.logger,
        "Hilo Worker iniciado para socket %d. Retardo de operación: %u ms.",
        fd_worker,
        g_storage_config.retardo_operacion
    );

    while (conectado) {

        uint16_t op_code = 0;
        t_paquete paquete_recibido;
        paquete_iniciar(&paquete_recibido);

        int res = recibir_paquete(fd_worker, &op_code, &paquete_recibido);

        if (res != 0) {
            conectado = false;
            paquete_destruir(&paquete_recibido);
            continue;
        }

        switch (op_code) {

            case OP_GET_BLOCK_SIZE: {
                // ----------------------------------------------------------------
                // NUEVO: leer worker_id desde el payload del handshake
                // (requiere t_get_block_size_req en proto.h)
                // ----------------------------------------------------------------
                t_get_block_size_req* hreq =
                    (t_get_block_size_req*) paquete_leer_struct(&paquete_recibido, sizeof(t_get_block_size_req));

                if (hreq == NULL) {
                    log_error(
                        g_storage_config.logger,
                        "HANDSHAKE OP_GET_BLOCK_SIZE: paquete inválido (sin worker_id)."
                    );

                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);

                    conectado = false;
                    break;
                }

                worker_id = hreq->worker_id;
                free(hreq);

                if (!worker_registrado) {
                    log_worker_conexion(worker_id);
                    worker_registrado = true;
                }

                log_info(
                    g_storage_config.logger,
                    "Recibido HANDSHAKE (%hu) de Worker %u. Retornando BLOCK_SIZE.",
                    op_code,
                    worker_id
                );

                t_paquete p_resp;
                paquete_iniciar(&p_resp);

                t_block_size b_size_msg = { .block_size = g_storage_config.block_size };

                if (paquete_cargar_struct(&p_resp, &b_size_msg, sizeof(t_block_size)) != 0) {
                    log_error(
                        g_storage_config.logger,
                        "Fallo al cargar t_block_size al paquete de respuesta (socket %d).",
                        fd_worker
                    );
                    paquete_destruir(&p_resp);
                    conectado = false;
                    break;
                }

                if (enviar_paquete(fd_worker, OP_BLOCK_SIZE, &p_resp) != 0) {
                    log_error(
                        g_storage_config.logger,
                        "Fallo al enviar respuesta al worker (socket %d): %s",
                        fd_worker,
                        strerror(errno)
                    );
                    conectado = false;
                } else {
                    log_info(
                        g_storage_config.logger,
                        "BLOCK_SIZE (%u) enviado exitosamente al worker (socket %d).",
                        g_storage_config.block_size,
                        fd_worker
                    );
                }

                paquete_destruir(&p_resp);
                break;
            }

            case OP_READ_BLOCK: {
                t_read_req* req =
                    (t_read_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_read_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "READ_BLOCK: paquete inválido.");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(
                    g_storage_config.logger,
                    "[Query %u] READ path=%s block_idx=%u",
                    req->query_id,
                    req->path,
                    req->block_idx
                );

                usleep(g_storage_config.retardo_operacion * 1000);

                uint32_t bs = g_storage_config.block_size;
                void* tmp = malloc(bs);
                if (!tmp) {
                    log_error(
                        g_storage_config.logger,
                        "[Query %u] READ_BLOCK: sin memoria para buffer de %u bytes.",
                        req->query_id,
                        bs
                    );
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    free(req);
                    break;
                }

                // usa api vieja
                ssize_t leidos = fs_leer_bloque(
                    req->query_id,
                    g_storage_config.punto_montaje,
                    req->path,
                    req->block_idx,
                    tmp,
                    bs
                );

                t_paquete resp;
                paquete_iniciar(&resp);

                if (leidos < 0) {
                    log_error(
                        g_storage_config.logger,
                        "[Query %u] READ_BLOCK: fs_leer_bloque falló para %s bloque %u",
                        req->query_id,
                        req->path,
                        req->block_idx
                    );
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                } else {
                    paquete_cargar_struct(&resp, tmp, (uint32_t)leidos);
                    enviar_paquete(fd_worker, OP_BLOCK_DATA, &resp);
                }

                paquete_destruir(&resp);
                free(tmp);
                free(req);
                break;
            }

            case OP_WRITE_BLOCK: {
                t_write_req* req =
                    (t_write_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_write_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "WRITE_BLOCK: paquete inválido (cabecera).");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(
                    g_storage_config.logger,
                    "[Query %u] WRITE path=%s block_idx=%u len=%u",
                    req->query_id,
                    req->path,
                    req->block_idx,
                    req->len
                );

                usleep(g_storage_config.retardo_operacion * 1000);

                if (req->len > g_storage_config.block_size) {
                    log_error(
                        g_storage_config.logger,
                        "[Query %u] WRITE_BLOCK: len=%u supera block_size=%u",
                        req->query_id,
                        req->len,
                        g_storage_config.block_size
                    );
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    free(req);
                    break;
                }

                void* payload = NULL;
                if (req->len > 0) {
                    payload = paquete_leer_struct(&paquete_recibido, req->len);
                    if (payload == NULL) {
                        log_error(g_storage_config.logger, "[Query %u] WRITE_BLOCK: payload inválido.", req->query_id);
                        t_paquete resp; paquete_iniciar(&resp);
                        enviar_paquete(fd_worker, OP_ERROR, &resp);
                        paquete_destruir(&resp);
                        free(req);
                        break;
                    }
                }

                bool ok = fs_write_bloque(req->query_id, req->path, req->block_idx, payload, req->len);

                t_paquete resp;
                paquete_iniciar(&resp);

                if (ok) enviar_paquete(fd_worker, OP_OK, &resp);
                else    enviar_paquete(fd_worker, OP_ERROR, &resp);

                paquete_destruir(&resp);
                if (payload) free(payload);
                free(req);
                break;
            }

            case OP_COMMIT: {
                t_commit_req* req =
                    (t_commit_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_commit_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "COMMIT: paquete inválido.");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(g_storage_config.logger, "[Query %u] COMMIT recibido para %s", req->query_id, req->path);
                usleep(g_storage_config.retardo_operacion * 1000);

                bool ok = fs_commit(req->query_id, req->path);

                t_paquete resp;
                paquete_iniciar(&resp);

                if (ok) enviar_paquete(fd_worker, OP_OK, &resp);
                else    enviar_paquete(fd_worker, OP_ERROR, &resp);

                paquete_destruir(&resp);
                free(req);
                break;
            }

            case OP_TRUNCATE: {
                t_truncate_req* req =
                    (t_truncate_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_truncate_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "TRUNCATE: paquete inválido (tamaño insuficiente).");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(
                    g_storage_config.logger,
                    "[Query %u] TRUNCATE path=%s new_size=%u",
                    req->query_id,
                    req->path,
                    req->new_size
                );

                usleep(g_storage_config.retardo_operacion * 1000);

                bool ok = fs_truncate(req->query_id, req->path, req->new_size);

                t_paquete resp;
                paquete_iniciar(&resp);

                if (ok) enviar_paquete(fd_worker, OP_OK, &resp);
                else    enviar_paquete(fd_worker, OP_ERROR, &resp);

                paquete_destruir(&resp);
                free(req);
                break;
            }

            case OP_TAG: {
                t_tag_req* req =
                    (t_tag_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_tag_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "TAG: paquete inválido.");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(
                    g_storage_config.logger,
                    "[Query %u] TAG src=%s dst=%s",
                    req->query_id,
                    req->src,
                    req->dst
                );

                usleep(g_storage_config.retardo_operacion * 1000);

                bool ok = fs_tag(req->query_id, req->src, req->dst);

                t_paquete resp; paquete_iniciar(&resp);

                if (ok) enviar_paquete(fd_worker, OP_OK, &resp);
                else    enviar_paquete(fd_worker, OP_ERROR, &resp);

                paquete_destruir(&resp);
                free(req);
                break;
            }

            case OP_DELETE: {
                t_delete_req* req =
                    (t_delete_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_delete_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "DELETE: paquete inválido.");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(g_storage_config.logger, "[Query %u] DELETE path=%s", req->query_id, req->path);
                usleep(g_storage_config.retardo_operacion * 1000);

                bool ok = fs_delete(req->query_id, req->path);

                t_paquete resp;
                paquete_iniciar(&resp);

                if (ok) enviar_paquete(fd_worker, OP_OK, &resp);
                else    enviar_paquete(fd_worker, OP_ERROR, &resp);

                paquete_destruir(&resp);
                free(req);
                break;
            }

            case OP_CREATE: {
                t_create_req* req =
                    (t_create_req*)paquete_leer_struct(&paquete_recibido, sizeof(t_create_req));

                if (req == NULL) {
                    log_error(g_storage_config.logger, "CREATE: paquete inválido (tamaño insuficiente).");
                    t_paquete resp; paquete_iniciar(&resp);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                    paquete_destruir(&resp);
                    conectado = false;
                    break;
                }

                log_info(
                    g_storage_config.logger,
                    "[Query %u] CREATE recibido para File:Tag=%s (punto_montaje=%s)",
                    req->query_id,
                    req->path,
                    g_storage_config.punto_montaje
                );

                usleep(g_storage_config.retardo_operacion * 1000);

                bool ok = fs_crear_file_tag(req->query_id, g_storage_config.punto_montaje, req->path);

                t_paquete resp;
                paquete_iniciar(&resp);

                if (ok) {
                    log_info(g_storage_config.logger, "[Query %u] CREATE OK: creado directorio para %s.", req->query_id, req->path);
                    enviar_paquete(fd_worker, OP_OK, &resp);
                } else {
                    log_error(g_storage_config.logger, "[Query %u] CREATE ERROR: no se pudo crear %s.", req->query_id, req->path);
                    enviar_paquete(fd_worker, OP_ERROR, &resp);
                }

                paquete_destruir(&resp);
                free(req);
                break;
            }

            default: {
                t_paquete p; paquete_iniciar(&p);
                enviar_paquete(fd_worker, OP_ERROR, &p);
                paquete_destruir(&p);

                log_warning(
                    g_storage_config.logger,
                    "Opcode desconocido (%hu) → OP_ERROR y cierre.",
                    op_code
                );

                conectado = false;
                break;
            }
        }

        paquete_destruir(&paquete_recibido);
    }

    close(fd_worker);

    // ------------------------------------------------------------------------
    // NUEVO: log obligatorio de desconexión (si llegamos a registrar worker_id)
    // ------------------------------------------------------------------------
    if (worker_registrado) {
        log_worker_desconexion(worker_id);
    }

    log_info(g_storage_config.logger, "Conexión con Worker (socket %d) finalizada.", fd_worker);
    return NULL;
}

// escuchar workers
int iniciar_servidor_storage(const char* puerto) {
    int fd_servidor = escuchar_en(puerto);
    if (fd_servidor < 0) {
        log_error(g_storage_config.logger, "Fallo al iniciar el servidor en puerto %s.", puerto);
        return -1;
    }

    log_info(
        g_storage_config.logger,
        "Servidor inicializado en puerto %s (Socket FD: %d).",
        puerto,
        fd_servidor
    );

    struct sockaddr_storage client_addr;
    socklen_t client_size = sizeof(client_addr);

    while (1) {
        int fd_worker = accept(fd_servidor, (struct sockaddr*)&client_addr, &client_size);

        if (fd_worker == -1) {
            log_error(g_storage_config.logger, "Error al aceptar conexión: %s", strerror(errno));
            continue;
        }

        log_info(g_storage_config.logger, "Nueva conexión de Worker. Socket: %d", fd_worker);

        int* fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) {
            log_error(g_storage_config.logger, "Fallo al reservar memoria para el FD del Worker.");
            close(fd_worker);
            continue;
        }
        *fd_ptr = fd_worker;

        pthread_t worker_thread;

        if (pthread_create(&worker_thread, NULL, atender_worker, fd_ptr) != 0) {
            log_error(
                g_storage_config.logger,
                "Fallo al crear el hilo Worker para socket %d: %s",
                fd_worker,
                strerror(errno)
            );
            free(fd_ptr);
            close(fd_worker);
        } else {
            pthread_detach(worker_thread);
        }
    }

    close(fd_servidor);
    fs_desmapear_data_blocks();
    return 0;
}
