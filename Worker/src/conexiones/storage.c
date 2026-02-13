// ============================================================================
// WORKER - storage.c
// PASO A PASO GENERAL
// 1) Define estructuras de mensajes para el protocolo con Storage
// 2) Arma paths FILE:TAG
// 3) Envía pedidos a Storage y espera OK/ERROR
// 4) Obtiene BLOCK_SIZE
// 5) Ejecuta CREATE / TAG / TRUNCATE / COMMIT / DELETE
// 6) Ejecuta READ_BLOCK y WRITE_BLOCK
// ============================================================================

#include "storage.h"
#include "../../../utils/src/proto.h"
#include "../../../utils/src/paquete.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <commons/log.h>

#define W_PATH_MAX M_MAX_PATH

// ---------------------------------------------------------------------------
// Contexto de Query para enviar en todos los mensajes a Storage.
// Se setea desde instrucciones.c (y cualquier otro callsite que ejecute queries).
// ---------------------------------------------------------------------------
#if defined(__GNUC__)
__thread uint32_t g_worker_query_id = 0;
#else
uint32_t g_worker_query_id = 0;
#endif

// ---------------------------------------------------------------------------
// NUEVO: worker_id para handshake con Storage (para logs de conexión/desconexión)
// Se setea desde el init del Worker cuando leés la config.
// ---------------------------------------------------------------------------
static uint32_t g_worker_id = 0;

void storage_set_worker_id(uint32_t worker_id) {
    g_worker_id = worker_id;
}

// ---------------------------------------------------------------------------
// Estructuras de protocolo lado Worker (deben matchear Storage)
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t query_id;
    char     path[W_PATH_MAX];
    uint32_t block_idx;
} t_read_req_net;

typedef struct {
    uint32_t query_id;
    char     path[W_PATH_MAX];
    uint32_t block_idx;
    uint32_t len;
} t_write_req_net;

typedef struct __attribute__((__packed__)) {
    uint32_t query_id;
    char     path[W_PATH_MAX];
} t_create_req_net;

typedef struct __attribute__((__packed__)) {
    uint32_t query_id;
    char     path[W_PATH_MAX];
    uint32_t new_size;
} t_truncate_req_net;

typedef struct __attribute__((__packed__)) {
    uint32_t query_id;
    char     path[W_PATH_MAX];
} t_commit_req;

typedef struct {
    uint32_t query_id;
    char     src[W_PATH_MAX];
    char     dst[W_PATH_MAX];
} t_tag_req_net;

typedef struct {
    uint32_t query_id;
    char     path[W_PATH_MAX];
} t_delete_req_net;

typedef struct {
    uint32_t block_size;
} t_block_size_net;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void storage_set_query_id(uint32_t query_id) {
    g_worker_query_id = query_id;
}

static void build_path(char* dst, size_t max, file_tag_t ft) {
    const char* f = ft.file ? ft.file : "";
    const char* t = ft.tag  ? ft.tag  : "";

    if (t[0] == '\0')
        snprintf(dst, max, "%s", f);
    else
        snprintf(dst, max, "%s:%s", f, t);
}

static int esperar_ok_error(int fd, t_log* logger, const char* ctx) {
    uint16_t op_resp = 0;
    t_paquete resp;
    paquete_iniciar(&resp);

    if (recibir_paquete(fd, &op_resp, &resp) != 0) {
        if (logger) log_error(logger, "[STORAGE] %s: error recibiendo respuesta.", ctx);
        paquete_destruir(&resp);
        return 0;
    }

    if (op_resp == OP_OK) {
        paquete_destruir(&resp);
        return 1;
    }

    if (op_resp == OP_ERROR) {
        if (logger) log_error(logger, "[STORAGE] %s devolvió OP_ERROR.", ctx);
        paquete_destruir(&resp);
        return 0;
    }

    if (logger) {
        log_error(
            logger,
            "[STORAGE] %s: opcode inesperado %hu (esperaba OP_OK/OP_ERROR).",
            ctx,
            op_resp
        );
    }

    paquete_destruir(&resp);
    return 0;
}

// ---------------------------------------------------------------------------
// GET BLOCK SIZE (HANDSHAKE)
// ---------------------------------------------------------------------------
int storage_get_block_size(int fd_storage, t_log* logger) {
    t_paquete p;
    paquete_iniciar(&p);

    // -----------------------------------------------------------------------
    // NUEVO: enviar worker_id en el payload del OP_GET_BLOCK_SIZE
    // (Storage lo usa para loguear conexión/desconexión por ID).
    // -----------------------------------------------------------------------
    t_get_block_size_req hreq = { .worker_id = g_worker_id };

    if (paquete_cargar_struct(&p, &hreq, sizeof(hreq)) != 0) {
        if (logger) log_error(logger, "[STORAGE] Error armando OP_GET_BLOCK_SIZE (handshake).");
        paquete_destruir(&p);
        return -1;
    }

    if (enviar_paquete(fd_storage, OP_GET_BLOCK_SIZE, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] Error enviando OP_GET_BLOCK_SIZE.");
        paquete_destruir(&p);
        return -1;
    }
    paquete_destruir(&p);

    uint16_t op_resp = 0;
    t_paquete resp;
    paquete_iniciar(&resp);

    if (recibir_paquete(fd_storage, &op_resp, &resp) != 0) {
        if (logger) log_error(logger, "[STORAGE] Error recibiendo BLOCK_SIZE.");
        paquete_destruir(&resp);
        return -1;
    }

    if (op_resp != OP_BLOCK_SIZE) {
        if (logger) {
            log_error(
                logger,
                "[STORAGE] Esperaba OP_BLOCK_SIZE y recibí %u.",
                op_resp
            );
        }
        paquete_destruir(&resp);
        return -1;
    }

    resp.offset = 0;
    t_block_size_net* raw =
        (t_block_size_net*) paquete_leer_struct(&resp, sizeof(t_block_size_net));
    if (!raw) {
        if (logger) log_error(logger, "[STORAGE] No pude leer BLOCK_SIZE.");
        paquete_destruir(&resp);
        return -1;
    }

    uint32_t bs_host = raw->block_size;
    free(raw);
    paquete_destruir(&resp);

    if (logger) log_info(logger, "[STORAGE] BLOCK_SIZE = %u bytes.", bs_host);
    return (int)bs_host;
}

// ---------------------------------------------------------------------------
// CREATE
// ---------------------------------------------------------------------------
int storage_create(file_tag_t ft, int fd_storage, t_log* logger) {
    char path[W_PATH_MAX];
    build_path(path, sizeof(path), ft);

    t_create_req_net req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;
    strncpy(req.path, path, sizeof(req.path) - 1);

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] CREATE: error armando paquete.");
        paquete_destruir(&p);
        return 0;
    }

    if (enviar_paquete(fd_storage, OP_CREATE, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] CREATE: error enviando OP_CREATE.");
        paquete_destruir(&p);
        return 0;
    }
    paquete_destruir(&p);

    return esperar_ok_error(fd_storage, logger, "CREATE");
}

// ---------------------------------------------------------------------------
// TAG
// ---------------------------------------------------------------------------
int storage_tag(file_tag_t origen, file_tag_t destino, int fd_storage, t_log* logger) {
    char src[W_PATH_MAX];
    char dst[W_PATH_MAX];
    build_path(src, sizeof(src), origen);
    build_path(dst, sizeof(dst), destino);

    t_tag_req_net req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;
    strncpy(req.src, src, sizeof(req.src) - 1);
    strncpy(req.dst, dst, sizeof(req.dst) - 1);

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] TAG: error armando paquete.");
        paquete_destruir(&p);
        return 0;
    }

    if (enviar_paquete(fd_storage, OP_TAG, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] TAG: error enviando OP_TAG.");
        paquete_destruir(&p);
        return 0;
    }
    paquete_destruir(&p);

    return esperar_ok_error(fd_storage, logger, "TAG");
}

// ---------------------------------------------------------------------------
// TRUNCATE
// ---------------------------------------------------------------------------
int storage_truncate(file_tag_t ft, uint32_t nuevo_tam_bytes, int fd_storage, t_log* logger) {
    char path[W_PATH_MAX];
    build_path(path, sizeof(path), ft);

    t_truncate_req_net req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;
    strncpy(req.path, path, sizeof(req.path) - 1);
    req.new_size = nuevo_tam_bytes;

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] TRUNCATE: error armando paquete.");
        paquete_destruir(&p);
        return 0;
    }

    if (enviar_paquete(fd_storage, OP_TRUNCATE, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] TRUNCATE: error enviando OP_TRUNCATE.");
        paquete_destruir(&p);
        return 0;
    }
    paquete_destruir(&p);

    return esperar_ok_error(fd_storage, logger, "TRUNCATE");
}

// ---------------------------------------------------------------------------
// COMMIT
// ---------------------------------------------------------------------------
int storage_commit(file_tag_t ft, int fd_storage, t_log* logger) {
    t_commit_req req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;

    // Path FILE:TAG consistente con el resto
    build_path(req.path, sizeof(req.path), ft);

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] COMMIT: error armando paquete.");
        paquete_destruir(&p);
        return 0;
    }

    if (enviar_paquete(fd_storage, OP_COMMIT, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] COMMIT: error enviando OP_COMMIT.");
        paquete_destruir(&p);
        return 0;
    }

    paquete_destruir(&p);
    return esperar_ok_error(fd_storage, logger, "COMMIT");
}

// ---------------------------------------------------------------------------
// DELETE
// ---------------------------------------------------------------------------
int storage_delete(file_tag_t ft, int fd_storage, t_log* logger) {
    char path[W_PATH_MAX];
    build_path(path, sizeof(path), ft);

    t_delete_req_net req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;
    strncpy(req.path, path, sizeof(req.path) - 1);

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] DELETE: error armando paquete.");
        paquete_destruir(&p);
        return 0;
    }

    if (enviar_paquete(fd_storage, OP_DELETE, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] DELETE: error enviando OP_DELETE.");
        paquete_destruir(&p);
        return 0;
    }
    paquete_destruir(&p);

    return esperar_ok_error(fd_storage, logger, "DELETE");
}

// ---------------------------------------------------------------------------
// READ BLOCK
// ---------------------------------------------------------------------------
int storage_io_read_block(
    file_tag_t ft,
    uint32_t   block_id,
    int        fd_storage,
    t_log*     logger,
    char*      destino,
    uint32_t   max_bytes
) {
    char path[W_PATH_MAX];
    build_path(path, sizeof(path), ft);

    t_read_req_net req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;
    strncpy(req.path, path, sizeof(req.path) - 1);
    req.block_idx = block_id;

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] READ_BLOCK: error armando paquete.");
        paquete_destruir(&p);
        return 0;
    }

    if (enviar_paquete(fd_storage, OP_READ_BLOCK, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] READ_BLOCK: error enviando OP_READ_BLOCK.");
        paquete_destruir(&p);
        return 0;
    }
    paquete_destruir(&p);

    uint16_t op_resp = 0;
    t_paquete resp;
    paquete_iniciar(&resp);

    if (recibir_paquete(fd_storage, &op_resp, &resp) != 0) {
        if (logger) log_error(logger, "[STORAGE] READ_BLOCK: error recibiendo respuesta.");
        paquete_destruir(&resp);
        return 0;
    }

    if (op_resp == OP_ERROR) {
        if (logger) log_error(logger, "[STORAGE] READ_BLOCK devolvió OP_ERROR.");
        paquete_destruir(&resp);
        return 0;
    }

    if (op_resp != OP_BLOCK_DATA) {
        if (logger) {
            log_error(
                logger,
                "[STORAGE] READ_BLOCK: opcode inesperado %hu (esperaba OP_BLOCK_DATA).",
                op_resp
            );
        }
        paquete_destruir(&resp);
        return 0;
    }

    resp.offset = 0;
    void* raw = paquete_leer_struct(&resp, max_bytes);
    if (!raw) {
        if (logger) log_error(logger, "[STORAGE] READ_BLOCK: error leyendo datos.");
        paquete_destruir(&resp);
        return 0;
    }

    memcpy(destino, raw, max_bytes);
    free(raw);
    paquete_destruir(&resp);
    return 1;
}

// ---------------------------------------------------------------------------
// WRITE BLOCK
// ---------------------------------------------------------------------------
int storage_io_write_block(
    file_tag_t   ft,
    uint32_t     block_id,
    const char*  origen,
    uint32_t     size,
    int          fd_storage,
    t_log*       logger
) {
    char path[W_PATH_MAX];
    build_path(path, sizeof(path), ft);

    t_write_req_net req;
    memset(&req, 0, sizeof(req));
    req.query_id = g_worker_query_id;
    strncpy(req.path, path, sizeof(req.path) - 1);
    req.block_idx = block_id;
    req.len       = size;

    t_paquete p;
    paquete_iniciar(&p);

    if (paquete_cargar_struct(&p, &req, sizeof(req)) != 0) {
        if (logger) log_error(logger, "[STORAGE] WRITE_BLOCK: error armando cabecera.");
        paquete_destruir(&p);
        return 0;
    }

    if (size > 0) {
        if (paquete_cargar_datos(&p, origen, size) != 0) {
            if (logger) log_error(logger, "[STORAGE] WRITE_BLOCK: error cargando datos.");
            paquete_destruir(&p);
            return 0;
        }
    }

    if (enviar_paquete(fd_storage, OP_WRITE_BLOCK, &p) != 0) {
        if (logger) log_error(logger, "[STORAGE] WRITE_BLOCK: error enviando OP_WRITE_BLOCK.");
        paquete_destruir(&p);
        return 0;
    }
    paquete_destruir(&p);

    return esperar_ok_error(fd_storage, logger, "WRITE_BLOCK");
}
