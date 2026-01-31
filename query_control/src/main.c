#include <commons/log.h>
#include <commons/config.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "../../utils/src/net.h"
#include "../../utils/src/proto.h"
#include "../../utils/src/paquete.h"

// Estructura para almacenar la configuración leída
typedef struct {
    char* ip_master;
    char* puerto_master;
    char* log_level;
} t_query_config;

// Función para leer el archivo de configuración
t_query_config* leer_config(const char* path) {
    t_config* cfg = config_create((char*) path);
    if (!cfg) return NULL;

    t_query_config* c = malloc(sizeof(t_query_config));
    c->ip_master = strdup(config_get_string_value(cfg, "IP_MASTER"));
    c->puerto_master = strdup(config_get_string_value(cfg, "PUERTO_MASTER"));
    c->log_level = strdup(config_get_string_value(cfg, "LOG_LEVEL"));
    config_destroy(cfg);
    return c;
}

// Función para liberar la memoria de la estructura de configuración
void destruir_config(t_query_config* c) {
    if (!c) return;
    free(c->ip_master);
    free(c->puerto_master);
    free(c->log_level);
    free(c);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s [archivo_config] [archivo_query] [prioridad]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* path_config = argv[1];
    const char* path_query  = argv[2];
    uint32_t prioridad      = (uint32_t) atoi(argv[3]);

    t_query_config* cfg = leer_config(path_config);
    if (!cfg) {
        fprintf(stderr, "Error leyendo archivo de configuración\n");
        return EXIT_FAILURE;
    }

    t_log* logger = log_create(
        "query_control.log",
        "QUERY_CONTROL",
        true,
        log_level_from_string(cfg->log_level)
    );

    int fd_master = conectar_a(cfg->ip_master, cfg->puerto_master);
    if (fd_master == -1) {
        log_error(logger, "No se pudo conectar al Master (%s:%s) - Motivo: %s",
                  cfg->ip_master, cfg->puerto_master, strerror(errno));
        destruir_config(cfg);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    log_info(logger,
             "## Conexión al Master exitosa. IP: %s, Puerto: %s",
             cfg->ip_master, cfg->puerto_master);

    // =========================================================================
    // ENVÍO DE PAQUETE (tamaño variable): OP_SUBMIT_QUERY
    // =========================================================================
    t_paquete paquete_envio;
    paquete_iniciar(&paquete_envio);

    // Cargamos los campos individualmente
    paquete_cargar(&paquete_envio, &prioridad, sizeof(uint32_t));
    paquete_cargar_cstring(&paquete_envio, path_query); // incluye el '\0'

    enviar_paquete(fd_master, OP_SUBMIT_QUERY, &paquete_envio);
    log_info(logger,
             "## Solicitud de ejecución de Query: %s, prioridad: %u",
             path_query, prioridad);

    paquete_destruir(&paquete_envio);
    // =========================================================================

    // Bucle para esperar mensajes del Master
    bool seguir       = true;
    bool recibio_end  = false;

    while (seguir) {
        uint16_t op_code;
        t_paquete paquete_resp;
        paquete_iniciar(&paquete_resp);

        int res = recibir_paquete(fd_master, &op_code, &paquete_resp);

        if (res != 0) {
            // Si nunca recibimos QUERY_END, lo consideramos cierre inesperado
            if (!recibio_end) {
                log_warning(logger,
                            "Master cerró la conexión (res=%d) antes de QUERY_END. Saliendo.",
                            res);
            }
            paquete_destruir(&paquete_resp);
            break;
        }

        switch (op_code) {
            case OP_READ_RESULT: {
                char* contenido = (char*) paquete_resp.buffer.stream;
                log_info(logger, "## Lectura/ACK recibida: %s", contenido);
                break;
            }

            case OP_QUERY_END: {
                char* motivo = (char*) paquete_resp.buffer.stream;
                // Podés loguear motivo y el mensaje “Finalización exitosa”
                log_info(logger,
                         "## Query Finalizada - %s",
                         motivo && motivo[0] ? motivo : "Finalización exitosa de la Query");
                recibio_end = true;
                seguir      = false;
                break;
            }

            default:
                log_warning(logger,
                            "## Paquete recibido con OP_CODE desconocido: %u",
                            op_code);
                break;
        }

        paquete_destruir(&paquete_resp);
    }

    // Limpieza de recursos
    close(fd_master);
    destruir_config(cfg);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
