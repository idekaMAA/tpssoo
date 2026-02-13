// ============================================================================
// WORKER - query_interpreter.c
// PASO A PASO GENERAL
// 1) Armar el path completo del archivo de Query
// 2) Loguear la recepción de la Query con su path
// 3) Posicionarse en el PC inicial (saltando comentarios/blancos)
// 4) Ejecutar línea a línea: parsear, loguear FETCH, ejecutar instrucción
// 5) Loguear instrucción realizada y actualizar PC
// 6) Finalizar Query con END explícito, error o END implícito
// ============================================================================

#include "query_interpreter.h"
#include "instrucciones_parser.h"
#include "instrucciones.h"
#include "../memoria_interna/memoria_interna.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <commons/log.h>
#include <sys/select.h>
#include <errno.h>

// Ajustá estos paths si tu estructura de carpetas es distinta:
#include "../conexiones/master.h"
#include "../../../utils/src/net.h"
#include "../../../utils/src/paquete.h"
#include "../../../utils/src/proto.h"
#include "../conexiones/storage.h" 

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------
static inline int _es_blanco_o_comentario(const char* s) {
    if (!s) return 1;
    while (*s == ' ' || *s == '\t') s++;
    return (*s == '\0' || *s == '#' || *s == ';');
}

static void _armar_fullpath(char* dst, size_t dstsz, const char* base, const char* file) {
    if (!base || !base[0]) {
        snprintf(dst, dstsz, "%s", file ? file : "");
        return;
    }
    size_t n = strlen(base);
    int barra = (n > 0 && base[n - 1] == '/');
    if (barra) snprintf(dst, dstsz, "%s%s", base, file ? file : "");
    else       snprintf(dst, dstsz, "%s/%s", base, file ? file : "");
}

static void _nombre_instr_de_linea(const char* line, char* out, size_t outsz) {
    out[0] = '\0';
    if (!line || !out || outsz == 0) return;

    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    size_t i = 0;
    while (*p && *p != ' ' && *p != '\t' && i + 1 < outsz) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

// Leemos un u32 del payload de un paquete (mismo estilo que en main.c)
static int _leer_u32_payload(const t_paquete* p, size_t* off, uint32_t* out) {
    if (!p || !out || !off) return -1;
    if (*off + sizeof(uint32_t) > p->buffer.size) return -1;
    memcpy(out, (const char*) p->buffer.stream + *off, sizeof(uint32_t));
    *off += sizeof(uint32_t);
    return 0;
}

// ---------------------------------------------------------------------------
// Chequeo de desalojo desde Master (no bloqueante)
// ---------------------------------------------------------------------------
// Devuelve:
//   1 -> hubo desalojo para ESTA query y YA se atendió (flush + ACK)
//   0 -> no hay nada (o mensaje no relevante), seguir ejecutando
//  -1 -> error grave de socket / protocolo
static int check_desalojo_desde_master(
    int         fd_master,
    uint32_t    query_id,
    t_log*      logger
) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_master, &readfds);

    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 0;  // timeout 0 => "asomo la cabeza" pero no bloqueo

    int r = select(fd_master + 1, &readfds, NULL, NULL, &tv);
    if (r < 0) {
        log_error(logger, "select() falló en Worker durante ejecución de Query %u: %s",
                  query_id, strerror(errno));
        return -1;
    }

    if (r == 0) {
        // No hay datos pendientes de Master
        return 0;
    }

    // Hay algo para leer del Master
    uint16_t op = 0;
    t_paquete p_rx;
    paquete_iniciar(&p_rx);

    int rc = recibir_paquete(fd_master, &op, &p_rx);
    if (rc != 0) {
        log_error(
            logger,
            "Error recibiendo mensaje de Master mientras ejecutaba Query %u (rc=%d)",
            query_id,
            rc
        );
        paquete_destruir(&p_rx);
        return -1;
    }

    if (op == OP_DESALOJO_QUERY || op == OP_DESALOJO_POR_CANCELACION) {
        size_t   off    = 0;
        uint32_t qid_rx = 0;

        if (_leer_u32_payload(&p_rx, &off, &qid_rx) != 0) {
            log_error(
                logger,
                "Payload de DESALOJO inválido durante ejecución de Query %u",
                query_id
            );
            paquete_destruir(&p_rx);
            return -1;
        }

        if (qid_rx != query_id) {
            // No es para esta Query. Por simplicidad lo ignoramos y seguimos.
            log_warning(
                logger,
                "Recibí DESALOJO para Query %u pero estoy ejecutando %u. Ignoro.",
                qid_rx,
                query_id
            );
            paquete_destruir(&p_rx);
            return 0;
        }

        // Log obligatorio de Desalojo de Query (warning para que resalte)
        log_warning(
            logger,
            "## Query %u: Desalojada por pedido del Master (opcode=%u)",
            query_id,
            op
        );

        uint32_t pc_actual = query_pc_actual(query_id);
        memoria_flush_implicito(query_id);

        int rc_ack = 0;
        if (op == OP_DESALOJO_QUERY) {
            rc_ack = master_enviar_desalojo_prioridad_ok(fd_master, query_id, pc_actual);
        } else { // OP_DESALOJO_POR_CANCELACION
            rc_ack = master_enviar_desalojo_cancelacion_ok(fd_master, query_id, pc_actual);
        }

        if (rc_ack != 0) {
            log_error(
                logger,
                "Error enviando ACK de desalojo (Q:%u, PC:%u, opcode=%u)",
                query_id,
                pc_actual,
                op
            );
            paquete_destruir(&p_rx);
            return -1;
        }

        paquete_destruir(&p_rx);
        // Avisamos al intérprete: se desalojó esta Query y ya se atendió todo
        return 1;
    }

    // Mensaje inesperado mientras ejecutamos la Query: lo logueamos y lo descartamos.
    log_warning(
        logger,
        "Mensaje inesperado (opcode=%u) mientras ejecutaba Query %u. Se descarta.",
        op,
        query_id
    );
    paquete_destruir(&p_rx);
    return 0;
}

// ---------------------------------------------------------------------------
// Ejecución de una Query
// ---------------------------------------------------------------------------
int ejecutar_query(
    uint32_t    query_id,
    const char* queries_path,
    const char* filename,
    uint32_t    pc_inicial,
    t_log*      logger,
    int         fd_storage,
    int         fd_master,
    uint32_t    retardo_ms
) {
    storage_set_query_id(query_id);
    // 1) Validar nombre de archivo
    if (!filename) {
        log_error(logger, "## Query %u: Filename inválido.", query_id);
        // Error de configuración de Query -> la consideramos QUERY_ERROR
        instr_end(query_id, logger, fd_master, pc_inicial, QUERY_ERROR);
        return 0;
    }

    // 2) Armar el path completo del archivo de Query
    char fullpath[512];
    _armar_fullpath(fullpath, sizeof(fullpath), queries_path, filename);

    // 3) Abrir el archivo de Query
    FILE* f = fopen(fullpath, "r");
    if (!f) {
        log_error(
            logger,
            "## Query %u: No pude abrir archivo %s. Verificar existencia y permisos.",
            query_id,
            fullpath
        );
        // No se pudo ni abrir el script -> QUERY_ERROR
        instr_end(query_id, logger, fd_master, pc_inicial, QUERY_ERROR);
        return 0;
    }

    // 4) Log obligatorio de recepción de Query con path de operaciones
    log_info(
        logger,
        "## Query %u: Se recibe la Query. El path de operaciones es: %s",
        query_id,
        fullpath
    );

    // 5) Preparar buffers y PC
    char*    line       = NULL;
    size_t   len        = 0;
    uint32_t pc         = 0;
    int      seguir     = 1;   // 1 = continuar, 0 = terminar
    int      desalojada = 0;   // 1 = la Query fue desalojada desde Master

    // 6) Avanzar hasta el PC inicial contando solo líneas ejecutables
    while (pc < pc_inicial) {
        ssize_t r = getline(&line, &len, f);
        if (r == -1) {
            log_error(
                logger,
                "## Query %u: PC inicial (%u) fuera de rango del archivo.",
                query_id,
                pc_inicial
            );
            free(line);
            fclose(f);
            // PC inicial inválido -> QUERY_ERROR
            instr_end(query_id, logger, fd_master, pc_inicial, QUERY_ERROR);
            return 0;
        }
        line[strcspn(line, "\r\n")] = 0;
        if (_es_blanco_o_comentario(line)) continue;
        pc++;
    }

    // 7) Registrar PC inicial para posible desalojo
    memoria_registrar_pc(query_id, pc);

    // 8) Loop principal de ejecución: leer línea a línea
    while (seguir && getline(&line, &len, f) != -1) {
        // 8.1) Normalizar fin de línea
        line[strcspn(line, "\r\n")] = 0;

        // 8.2) Saltar líneas blancas o comentarios
        if (_es_blanco_o_comentario(line)) {
            continue;
        }

        // 8.3) Antes de parsear/ejecutar, verificamos si Master pidió desalojo
        int chk = check_desalojo_desde_master(fd_master, query_id, logger);
        if (chk < 0) {
            // Error de comunicación grave → cortamos y devolvemos error
            log_error(
                logger,
                "## Query %u: Error de comunicación con Master durante ejecución. Abortando.",
                query_id
            );
            seguir = 0;
            // No mandamos instr_end acá porque el protocolo ya quedó roto
            break;
        }
        if (chk > 0) {
            // La Query fue desalojada y ya se avisó y flusheó dentro de check_desalojo.
            desalojada = 1;
            seguir     = 0;
            break;
        }

        // 8.4) Parsear línea a instruccion_t
        instruccion_t* inst = parsear_linea(line);
        if (!inst) {
            log_warning(
                logger,
                "## Query %u (PC:%u): Línea inválida. Deteniendo ejecución.",
                query_id,
                pc
            );

            // Consideramos esto error de la Query → END con error
            instr_end(query_id, logger, fd_master, pc, QUERY_ERROR);
            seguir = 0;
            break;
        }

        // 8.5) Obtener nombre de instrucción (sin parámetros)
        char instr_name[64];
        _nombre_instr_de_linea(line, instr_name, sizeof(instr_name));
        if (instr_name[0] == '\0') {
            strncpy(instr_name, "INSTRUCCION", sizeof(instr_name) - 1);
            instr_name[sizeof(instr_name) - 1] = '\0';
        }

        // 8.6) Log obligatorio de FETCH (sin parámetros)
        log_info(
            logger,
            "## Query %u: FETCH - Program Counter: %u - %s",
            query_id,
            pc,
            instr_name
        );

        // 8.7) Actualizar PC actual en memoria para desalojo
        memoria_registrar_pc(query_id, pc);

        // 8.8) Ejecutar instrucción
        // Contrato:
        //   rc > 0  -> instrucción OK, continuar
        //   rc == 0 -> END explícito
        //   rc < 0  -> error de instrucción
        int rc = ejecutar_instruccion(
            (int)query_id,
            inst,
            logger,
            fd_storage,
            fd_master,
            retardo_ms,
            (int)pc
        );

        if (rc > 0) {
            // 8.9) Ejecución OK: loguear y avanzar PC
            log_info(
                logger,
                "## Query %u: - Instrucción realizada: %s",
                query_id,
                instr_name
            );
            pc++;
            seguir = 1;
        } else {
            // rc == 0 -> END explícito
            // rc < 0  -> error
            if (rc == 0) {
                log_info(
                    logger,
                    "## Query %u (PC:%u): END alcanzado.",
                    query_id,
                    pc
                );
                // END explícito sin error → QUERY_OK
                instr_end(query_id, logger, fd_master, pc, QUERY_OK);
            } else {
                log_error(
                    logger,
                    "## Query %u (PC:%u): Error en instrucción '%s' (rc=%d).",
                    query_id,
                    pc,
                    instr_name,
                    rc
                );
                // Error en instrucción → QUERY_ERROR
                instr_end(query_id, logger, fd_master, pc, QUERY_ERROR);
            }

            seguir = 0;
        }

        destruir_instruccion(inst);
    }

    // 9) Liberar recursos de lectura y cerrar archivo
    if (line) free(line);
    fclose(f);

    // 10) Finalización según motivo
    if (!desalojada && seguir != 0) {
        // Llegamos al final de archivo sin END explícito y sin desalojo
        log_warning(
            logger,
            "## Query %u: Archivo terminó sin instrucción END. Finalización implícita.",
            query_id
        );
        // Finalización implícita: la tratamos como OK (no es un fallo de Storage/Memoria)
        instr_end(query_id, logger, fd_master, pc, QUERY_OK);
    }

    // 11) Devolver éxito lógico de la ejecución
    // - 0 sólo para errores "graves" (no pude abrir archivo, PC inicial inválido, etc.)
    // - para desalojo se devuelve 1 (no lo consideramos "fallo" para el main)
    return 1;
}
