#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <commons/log.h>
#include <commons/config.h>

#include "../../utils/src/net.h"
#include "../../utils/src/paquete.h"
#include "../../utils/src/proto.h"

#include "conexiones/master.h"
#include "conexiones/storage.h"
#include "memoria_interna/memoria_interna.h"
#include "query_interpreter/query_interpreter.h"

// ============================================================================
// WORKER - main.c
// PASO A PASO GENERAL
// 1) Configuración inicial y lectura de config
// 2) Conexión a Storage y handshake de BLOCK_SIZE
// 3) Inicialización de memoria interna
// 4) Conexión a Master (HELLO_WORKER)
// 5) Loop principal: recibir mensajes del Master (asignación / desalojo)
// 6) Liberación ordenada de recursos al finalizar
// ============================================================================

// ----------------- Globals -----------------
t_log* g_logger     = NULL;
int    g_fd_master  = -1;
int    g_fd_storage = -1;

// ----------------- Señales -----------------
static void sig_handler(int sig) {
    (void)sig;

    memoria_flush_global();

    if (g_logger) log_info(g_logger, "Signal recibida. Cerrando Worker…");
    if (g_fd_master  >= 0) close(g_fd_master);
    if (g_fd_storage >= 0) close(g_fd_storage);
    if (g_logger) log_destroy(g_logger);

    exit(0);
}

static void instalar_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

// ----------------- CFG helpers -----------------
static const char* cfg_get_str(t_config* cfg, const char* key_in, const char* nombre_cfg) {
    char* key = (char*) key_in;
    if (!config_has_property(cfg, key)) {
        fprintf(stderr, "[CFG] Falta la clave '%s' en %s\n", key, nombre_cfg);
        return NULL;
    }
    const char* v = config_get_string_value(cfg, key);
    if (!v) {
        fprintf(stderr, "[CFG] Clave '%s' nula en %s\n", key, nombre_cfg);
        return NULL;
    }
    return v;
}

static int cfg_get_int_chk(t_config* cfg, const char* key, const char* nombre_cfg, int* out) {
    const char* s = cfg_get_str(cfg, key, nombre_cfg);
    if (!s) return -1;
    char* end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        fprintf(stderr, "[CFG] Clave '%s' debe ser entero válido (valor: '%s')\n", key, s);
        return -1;
    }
    *out = (int)val;
    return 0;
}

// ----------------- Lectores de payload -----------------
static int leer_u32(const t_paquete* p, size_t* off, uint32_t* out) {
    if (!p || !out || !off) return -1;
    if (*off + sizeof(uint32_t) > p->buffer.size) return -1;
    memcpy(out, (const char*)p->buffer.stream + *off, sizeof(uint32_t));
    *off += sizeof(uint32_t);
    return 0;
}

static int leer_cstring_nt(const t_paquete* p, size_t* off, char** out_heap) {
    if (!p || !off || !out_heap) return -1;
    if (*off >= p->buffer.size) return -1;

    const char* base = (const char*)p->buffer.stream;
    size_t i = *off;
    while (i < p->buffer.size && base[i] != '\0') i++;
    if (i >= p->buffer.size) return -1;

    size_t len = i - *off;
    char* s = malloc(len + 1);
    if (!s) return -1;
    memcpy(s, base + *off, len);
    s[len] = '\0';

    *off = i + 1;
    *out_heap = s;
    return 0;
}

// ----------------- Main -----------------
int main(int argc, char** argv) {
    // 1) Validar parámetros y crear logger
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ruta_cfg> <worker_id>\n", argv[0]);
        return 1;
    }

    const char* ruta_cfg = argv[1];

    char* endid = NULL;
    long worker_id_l = strtol(argv[2], &endid, 10);
    if (endid == argv[2] || *endid != '\0' || worker_id_l < 0 || worker_id_l > 0xFFFFFFFFL) {
        fprintf(stderr, "worker_id inválido: '%s'\n", argv[2]);
        return 1;
    }
    uint32_t worker_id = (uint32_t)worker_id_l;

    g_logger = log_create("worker.log", "WORKER", true, LOG_LEVEL_INFO);
    if (!g_logger) {
        fprintf(stderr, "No pude crear logger\n");
        return 1;
    }

    instalar_signal_handlers();

    // 2) Leer archivo de configuración
    t_config* cfg = config_create((char*)ruta_cfg);
    if (!cfg) {
        log_error(g_logger, "No pude abrir config: %s", ruta_cfg);
        log_destroy(g_logger);
        return 1;
    }

    const char* ip_master        = cfg_get_str(cfg, "IP_MASTER", ruta_cfg);
    const char* puerto_master_s  = cfg_get_str(cfg, "PUERTO_MASTER", ruta_cfg);
    const char* ip_storage       = cfg_get_str(cfg, "IP_STORAGE", ruta_cfg);
    const char* puerto_storage_s = cfg_get_str(cfg, "PUERTO_STORAGE", ruta_cfg);
    const char* algoritmo_rep    = cfg_get_str(cfg, "ALGORITMO_REEMPLAZO", ruta_cfg);
    const char* path_scripts     = cfg_get_str(cfg, "PATH_SCRIPTS", ruta_cfg);

    int tam_memoria    = 0;
    int retardo_mem_ms = 0;

    if (!ip_master || !puerto_master_s || !ip_storage || !puerto_storage_s ||
        !algoritmo_rep || !path_scripts ||
        cfg_get_int_chk(cfg, "TAM_MEMORIA",     ruta_cfg, &tam_memoria) ||
        cfg_get_int_chk(cfg, "RETARDO_MEMORIA", ruta_cfg, &retardo_mem_ms)) {
        log_error(g_logger, "Config incompleta/incorrecta. Revisá %s", ruta_cfg);
        config_destroy(cfg);
        log_destroy(g_logger);
        return 1;
    }

    // 3) Conectar a Storage y hacer handshake de BLOCK_SIZE
    g_fd_storage = conectar_a(ip_storage, puerto_storage_s);
    if (g_fd_storage < 0) {
        log_error(g_logger, "No pude conectar a Storage %s:%s", ip_storage, puerto_storage_s);
        config_destroy(cfg);
        log_destroy(g_logger);
        return 1;
    }

    storage_set_worker_id(worker_id);
    
    int bs = storage_get_block_size(g_fd_storage, g_logger);
    if (bs <= 0) {
        log_error(g_logger, "Handshake con Storage falló o block_size inválido (%d)", bs);
        close(g_fd_storage);
        config_destroy(cfg);
        log_destroy(g_logger);
        return 1;
    }
    uint32_t block_size = (uint32_t)bs;
    log_info(g_logger, "Storage OK. block_size=%u", block_size);

    // 4) Inicializar memoria interna
    if (!memoria_init((uint32_t)tam_memoria, block_size, algoritmo_rep, g_logger)) {
        log_error(g_logger, "memoria_init falló");
        close(g_fd_storage);
        config_destroy(cfg);
        log_destroy(g_logger);
        return 1;
    }

    // 5) Conectar a Master y enviar HELLO_WORKER
    char* endpm = NULL;
    long pm = strtol(puerto_master_s, &endpm, 10);
    if (endpm == puerto_master_s || *endpm != '\0' || pm <= 0 || pm > 65535) {
        log_error(g_logger, "PUERTO_MASTER inválido: '%s'", puerto_master_s);
        close(g_fd_storage);
        memoria_destroy();
        config_destroy(cfg);
        log_destroy(g_logger);
        return 1;
    }

    g_fd_master = enviar_hello_worker(ip_master, (int)pm, g_logger, worker_id);
    if (g_fd_master < 0) {
        log_error(g_logger, "No pude conectar/enviar HELLO a Master %s:%s", ip_master, puerto_master_s);
        close(g_fd_storage);
        memoria_destroy();
        config_destroy(cfg);
        log_destroy(g_logger);
        return 1;
    }

    log_info(g_logger, "Worker %u listo. Esperando asignaciones…", worker_id);

    // 6) Loop principal de mensajes desde Master
    while (1) {
        uint16_t op = 0;
        t_paquete p_rx;
        paquete_iniciar(&p_rx);

        int rcv = recibir_paquete(g_fd_master, &op, &p_rx);
        if (rcv != 0) {
            log_error(g_logger, "Master desconectado o error de recv (rc=%d). Saliendo.", rcv);
            paquete_destruir(&p_rx);
            break;
        }

        if (op == OP_ASIGNACION_QUERY) {
            // payload: [u32 query_id][cstring filename][u32 pc_inicial]
            size_t   off = 0;
            uint32_t query_id = 0, pc_ini = 0;
            char*    filename = NULL;

            int ok = (leer_u32(&p_rx, &off, &query_id) == 0) &&
                     (leer_cstring_nt(&p_rx, &off, &filename) == 0) &&
                     (leer_u32(&p_rx, &off, &pc_ini) == 0);

            if (!ok) {
                log_error(
                    g_logger,
                    "ASIGNACION_QUERY: payload inválido (len=%u).",
                    p_rx.buffer.size
                );
                paquete_destruir(&p_rx);
                if (filename) free(filename);
                continue;
            }

            log_info(
                g_logger,
                "[ASSIGN] QID=%u file='%s' pc_inicial=%u",
                query_id,
                filename,
                pc_ini
            );

            int exec_ok = ejecutar_query(
                query_id,
                path_scripts,
                filename,
                pc_ini,
                g_logger,
                g_fd_storage,
                g_fd_master,
                (uint32_t)retardo_mem_ms
            );

            if (!exec_ok) {
                log_error(
                    g_logger,
                    "Fallo ejecutando Query %u.",
                    query_id
                );
            }

            free(filename);
        }

        else {
            log_warning(
                g_logger,
                "Opcode desconocido desde Master: %u (len=%u). Ignoro.",
                op,
                p_rx.buffer.size
            );
        }

        paquete_destruir(&p_rx);
    }

    // 7) Salida ordenada
    if (g_fd_master  >= 0) close(g_fd_master);
    if (g_fd_storage >= 0) close(g_fd_storage);

    memoria_destroy();
    config_destroy(cfg);

    log_info(g_logger, "Worker finalizado.");
    log_destroy(g_logger);

    return 0;
}
