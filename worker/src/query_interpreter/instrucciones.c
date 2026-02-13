// ============================================================================
// WORKER - instrucciones.c
// PASO A PASO GENERAL
// 1) Decodificar opcode y despachar a la función correspondiente
// 2) Ejecutar CREATE / TAG / TRUNCATE / WRITE / READ / COMMIT / FLUSH / DELETE / END
// 3) Usar memoria_interna para READ/WRITE/FLUSH y Storage para I/O persistente
// 4) Notificar al Master resultados de READ y END
// ============================================================================

#include "instrucciones.h"
#include "../../../utils/src/net.h"
#include "../../../utils/src/proto.h"
#include "../../../utils/src/paquete.h"
#include "../mensajes.h"
#include "../memoria_interna/memoria_interna.h"
#include "../conexiones/master.h"
#include "../conexiones/storage.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <commons/log.h>

// ---------------------------------------------------------------------------
// NORMALIZACIÓN File:Tag
// - Evita el bug de tener 2 representaciones:
//     (file="metroid", tag="v1") vs (file="metroid:v1", tag="")
// - Devuelve un file_tag_t que apunta a buffers provistos por el caller.
//   (válidos durante toda la ejecución de la instrucción actual)
// ---------------------------------------------------------------------------
static inline int _ft_tag_vacio(const char* t) {
    return (!t || t[0] == '\0');
}

static file_tag_t ft_normalize_to_buffers(
    file_tag_t in,
    char* file_buf, size_t file_buf_sz,
    char* tag_buf,  size_t tag_buf_sz
) {
    const char* f = in.file ? in.file : "";
    const char* t = in.tag  ? in.tag  : "";

    if (file_buf_sz > 0) file_buf[0] = '\0';
    if (tag_buf_sz  > 0) tag_buf[0]  = '\0';

    // Caso 1: viene separado correctamente (tag no vacío)
    if (!_ft_tag_vacio(t)) {
        if (file_buf_sz > 0) {
            strncpy(file_buf, f, file_buf_sz - 1);
            file_buf[file_buf_sz - 1] = '\0';
        }
        if (tag_buf_sz > 0) {
            strncpy(tag_buf, t, tag_buf_sz - 1);
            tag_buf[tag_buf_sz - 1] = '\0';
        }
        return (file_tag_t){ .file = file_buf, .tag = tag_buf };
    }

    // Caso 2: tag vacío pero file viene como "file:tag" -> split
    const char* sep = strchr(f, ':');
    if (sep) {
        size_t left_len = (size_t)(sep - f);

        if (file_buf_sz > 0) {
            size_t n = left_len < (file_buf_sz - 1) ? left_len : (file_buf_sz - 1);
            memcpy(file_buf, f, n);
            file_buf[n] = '\0';
        }

        const char* right = sep + 1;
        if (tag_buf_sz > 0) {
            strncpy(tag_buf, right, tag_buf_sz - 1);
            tag_buf[tag_buf_sz - 1] = '\0';
        }

        return (file_tag_t){ .file = file_buf, .tag = tag_buf };
    }

    // Caso 3: no hay ':' y tag vacío -> copiar file y dejar tag vacío
    if (file_buf_sz > 0) {
        strncpy(file_buf, f, file_buf_sz - 1);
        file_buf[file_buf_sz - 1] = '\0';
    }
    if (tag_buf_sz > 0) tag_buf[0] = '\0';

    return (file_tag_t){ .file = file_buf, .tag = tag_buf };
}

// ---------------------------------------------------------------------------
// Dispatcher de instrucciones
//
// Contrato con el intérprete:
//   rc > 0  -> instrucción OK, continuar
//   rc == 0 -> END explícito
//   rc < 0  -> error de instrucción (la Query termina por error)
// ---------------------------------------------------------------------------
int ejecutar_instruccion(
    int            query_id,
    instruccion_t* inst,
    t_log*         logger,
    int            fd_storage,
    int            fd_master,
    uint32_t       retardo_ms,
    int            pc_actual
) {
    // 1) Si es END, finalización lógica de la Query
    //    NO llamar a instr_end() acá. Solo devolvemos 0 y el intérprete
    //    se encarga de la finalización (flush + QUERY_END).
    if (strcmp(inst->opcode, "END") == 0) {
        return 0;
    }

    // 2) Validar que haya al menos un File:Tag
    if (inst->file_count <= 0) {
        log_error(
            logger,
            "[Q%d] Instrucción '%s' requiere File:Tag.",
            query_id,
            inst->opcode
        );
        return -1;  // error
    }

    // 3) Tomar File:Tag principal y segundo (si existe)
    //    + NORMALIZAR para evitar "file:tag" mezclado en el campo file.
    file_tag_t ft_raw      = inst->files[0];
    file_tag_t ft_dest_raw = (inst->file_count > 1)
                               ? inst->files[1]
                               : (file_tag_t){ NULL, NULL };

    char ft_file_buf[256], ft_tag_buf[256];
    char fd_file_buf[256], fd_tag_buf[256];

    file_tag_t ft = ft_normalize_to_buffers(
        ft_raw,
        ft_file_buf, sizeof(ft_file_buf),
        ft_tag_buf,  sizeof(ft_tag_buf)
    );

    file_tag_t ft_dest = ft_normalize_to_buffers(
        ft_dest_raw,
        fd_file_buf, sizeof(fd_file_buf),
        fd_tag_buf,  sizeof(fd_tag_buf)
    );

    // 4) Despachar según opcode
    if (strcmp(inst->opcode, "CREATE") == 0) {
        return instr_create(query_id, ft, logger, fd_storage);

    } else if (strcmp(inst->opcode, "TAG") == 0) {
        return instr_tag(query_id, ft, ft_dest, logger, fd_storage);

    } else if (strcmp(inst->opcode, "TRUNCATE") == 0) {
        return instr_truncate(query_id, inst, ft, logger, fd_storage);

    } else if (strcmp(inst->opcode, "WRITE") == 0) {
        return instr_write(query_id, inst, ft, logger, fd_storage, retardo_ms);

    } else if (strcmp(inst->opcode, "READ") == 0) {
        return instr_read(query_id, inst, ft, logger, fd_storage, fd_master, retardo_ms);

    } else if (strcmp(inst->opcode, "COMMIT") == 0) {
        return instr_commit(query_id, ft, logger, fd_storage);

    } else if (strcmp(inst->opcode, "FLUSH") == 0) {
        return instr_flush(query_id, ft, logger, fd_storage);

    } else if (strcmp(inst->opcode, "DELETE") == 0) {
        return instr_delete(query_id, ft, logger, fd_storage);
    }

    // 5) Si no se reconoce el opcode, loguear y considerar error
    log_warning(
        logger,
        "[Q%d] Instrucción '%s' desconocida.",
        query_id,
        inst->opcode
    );
    return -1;
}

// ---------------------------------------------------------------------------
// CREATE
// ---------------------------------------------------------------------------
int instr_create(
    int        query_id,
    file_tag_t ft,
    t_log*     logger,
    int        fd_storage
) {
    // 1) Loguear operación (sin parámetros extra)
    log_info(
        logger,
        "[Query %d] -> CREATE",
        query_id
    );

    // 2) Enviar CREATE a Storage
    if (!storage_create(ft, fd_storage, logger)) {
        log_error(
            logger,
            "[Q%d] Error en CREATE",
            query_id
        );
        return -1;
    }

    // 3) Éxito
    return 1;
}

// ---------------------------------------------------------------------------
// TAG
// ---------------------------------------------------------------------------
int instr_tag(
    int        query_id,
    file_tag_t ft_origen,
    file_tag_t ft_destino,
    t_log*     logger,
    int        fd_storage
) {
    // 1) Loguear operación
    log_info(
        logger,
        "[Query %d] -> TAG",
        query_id
    );

    // 2) Enviar TAG a Storage
    if (!storage_tag(ft_origen, ft_destino, fd_storage, logger)) {
        log_error(
            logger,
            "[Q%d] Error en TAG",
            query_id
        );
        return -1;
    }

    // 3) Éxito
    return 1;
}

// ---------------------------------------------------------------------------
// TRUNCATE
// ---------------------------------------------------------------------------
int instr_truncate(
    int            query_id,
    instruccion_t* inst,
    file_tag_t     ft,
    t_log*         logger,
    int            fd_storage
) {
    // 1) Validar que exista tamaño en los parámetros
    if (inst->param_count == 0 || !inst->params[0]) {
        log_error(
            logger,
            "[Q%d] TRUNCATE necesita el tamaño en bytes.",
            query_id
        );
        return -1;
    }

    // 2) Interpretar parámetro como cantidad de bytes
    uint32_t nuevo_tam_bytes = (uint32_t)atoi(inst->params[0]);

    // 3) Loguear operación
    log_info(
        logger,
        "[Query %d] -> TRUNCATE",
        query_id
    );

    // 4) Enviar TRUNCATE a Storage
    if (!storage_truncate(ft, nuevo_tam_bytes, fd_storage, logger)) {
        log_error(
            logger,
            "[Q%d] Error en TRUNCATE",
            query_id
        );
        return -1;
    }

    // 5) Éxito
    return 1;
}

// ---------------------------------------------------------------------------
// WRITE
// ---------------------------------------------------------------------------
int instr_write(
    int            query_id,
    instruccion_t* inst,
    file_tag_t     ft,
    t_log*         logger,
    int            fd_storage,
    uint32_t       retardo_ms
) {
    // 1) Validar parámetros (dir_base y contenido)
    if (inst->param_count < 2) {
        log_error(
            logger,
            "[Q%d] WRITE necesita 2 parámetros (dir_base, content).",
            query_id
        );
        return -1;
    }

    // 2) Obtener dirección base y contenido
    uint32_t    dir_base = (uint32_t)atoi(inst->params[0]);
    const char* content  = inst->params[1];
    uint32_t    size     = (uint32_t)strlen(content);

    // 3) Loguear operación (sin mostrar contenido)
    log_info(
        logger,
        "[Query %d] -> WRITE",
        query_id
    );

    // 4) Escribir en memoria interna (con retardo simulado)
    int rc = memoria_escribir(
        ft,
        dir_base,
        size,
        content,
        query_id,
        fd_storage,
        logger,
        retardo_ms
    );

    if (rc < 0) {
        log_error(
            logger,
            "[Q%d] Error en WRITE (memoria_escribir devolvió %d).",
            query_id,
            rc
        );
        return -1;
    }

    return 1;
}

// ---------------------------------------------------------------------------
// READ
// ---------------------------------------------------------------------------
int instr_read(
    int            query_id,
    instruccion_t* inst,
    file_tag_t     ft,
    t_log*         logger,
    int            fd_storage,
    int            fd_master,
    uint32_t       retardo_ms
) {
    // 1) Validar parámetros (dir_base y size)
    if (inst->param_count < 2) {
        log_error(
            logger,
            "[Q%d] READ necesita 2 parámetros (dir_base, size).",
            query_id
        );
        return -1;
    }

    // 2) Obtener dirección base y tamaño a leer
    uint32_t dir_base = (uint32_t)atoi(inst->params[0]);
    uint32_t size     = (uint32_t)atoi(inst->params[1]);

    // 3) Loguear operación
    log_info(
        logger,
        "[Query %d] -> READ",
        query_id
    );

    // 4) Reservar buffer para el contenido
    char* buffer = malloc(size + 1);
    if (!buffer) {
        log_error(
            logger,
            "[Q%d] READ: sin memoria para buffer de %u bytes.",
            query_id,
            size
        );
        return -1;
    }

    // 5) Leer desde memoria interna (que a su vez puede hablar con Storage)
    int rc = memoria_leer(
        ft,
        dir_base,
        size,
        buffer,
        query_id,
        fd_storage,
        logger,
        retardo_ms
    );

    // ⚠️ Si Storage / memoria fallan, NO mandamos nada al Master
    if (rc < 0) {
        log_error(
            logger,
            "[Q%d] READ falló (memoria_leer devolvió %d).",
            query_id,
            rc
        );
        free(buffer);
        return -1;   // el intérprete va a tomar esto como error de instrucción
    }

    // 6) Terminar string leído
    buffer[size] = '\0';

    // 7) Enviar resultado al Master
    if (enviar_resultado_a_master(fd_master, (uint32_t)query_id, buffer, logger) != 0) {
        log_error(
            logger,
            "[Q%d] READ ok en memoria, pero falló el envío a Master.",
            query_id
        );
        free(buffer);
        // Lo consideramos error, así la Query termina con QUERY_ERROR
        return -1;
    }

    log_info(
        logger,
        "[Q%d] READ enviado a Master. Bytes=%u",
        query_id,
        size
    );

    // 8) Liberar buffer y continuar
    free(buffer);
    return 1;
}

// ---------------------------------------------------------------------------
// COMMIT
// ---------------------------------------------------------------------------
int instr_commit(
    int        query_id,
    file_tag_t ft,
    t_log*     logger,
    int        fd_storage
) {
    // 1) Loguear operación
    log_info(
        logger,
        "[Query %d] -> COMMIT",
        query_id
    );

    // 2) Flushear páginas dirty de ese File:Tag a Storage
    int flushed = memoria_flush(ft, fd_storage, logger);

    // Si memoria_flush devuelve error, NO queremos seguir como si nada
    if (flushed < 0) {
        log_error(
            logger,
            "[Q%d] COMMIT: falló el FLUSH previo, abortando COMMIT.",
            query_id
        );
        return -1;
    }

    if (flushed == 0) {
        log_info(
            logger,
            "[Query %d] COMMIT: no había páginas dirty.",
            query_id
        );
    }

    // 3) Confirmar COMMIT con Storage
    if (!storage_commit(ft, fd_storage, logger)) {
        log_error(
            logger,
            "[Q%d] Error en COMMIT",
            query_id
        );
        return -1;
    }

    return 1;
}

// ---------------------------------------------------------------------------
// FLUSH
// ---------------------------------------------------------------------------
int instr_flush(
    int        query_id,
    file_tag_t ft,
    t_log*     logger,
    int        fd_storage
) {
    // 1) Loguear operación
    log_info(
        logger,
        "[Query %d] -> FLUSH",
        query_id
    );

    // 2) Flushear páginas dirty del File:Tag
    int flushed = memoria_flush(ft, fd_storage, logger);

    // Si memoria_flush devuelve error, NO queremos seguir como si nada
    if (flushed < 0) {
        log_error(
            logger,
            "[Q%d] Error en FLUSH (memoria_flush devolvió %d).",
            query_id,
            flushed
        );
        return -1;
    }

    if (flushed == 0) {
        log_info(
            logger,
            "[Query %d] FLUSH: no se persistieron páginas dirty.",
            query_id
        );
    }

    // 3) FLUSH solo sincroniza memoria/Storage; no avisa al Master
    return 1;
}

// ---------------------------------------------------------------------------
// DELETE
// ---------------------------------------------------------------------------
int instr_delete(
    int        query_id,
    file_tag_t ft,
    t_log*     logger,
    int        fd_storage
) {
    // 1) Loguear operación
    log_info(
        logger,
        "[Query %d] -> DELETE",
        query_id
    );

    // 2) Pedir DELETE a Storage
    if (!storage_delete(ft, fd_storage, logger)) {
        log_error(
            logger,
            "[Q%d] Error en DELETE",
            query_id
        );
        return -1;
    }

    return 1;
}

// ---------------------------------------------------------------------------
// END
// ---------------------------------------------------------------------------
int instr_end(
    int               query_id,
    t_log*            logger,
    int               fd_master,
    int               final_pc,
    t_query_resultado estado   // OK / ERROR / CANCELADA
) {
    // 1) Flush implícito (ya loguea adentro)
    //    END NORMAL NO PERSISTE: solo libera (descarta dirty si existiera).
    memoria_liberar_implicito((uint32_t)query_id);

    // 2) Notificar END al Master, con el estado recibido
    if (enviar_end_a_master(
            fd_master,
            (uint32_t)query_id,
            (uint32_t)final_pc,
            estado,
            logger) != 0) {
        log_error(logger, "[Q%d] Error enviando END a Master.", query_id);
        return -1;
    }

    log_info(
        logger,
        "## Query %d: - Instrucción realizada: END (estado=%d)",
        query_id,
        estado
    );
    return 0;
}
