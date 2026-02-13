#include "memoria_interna.h"
#include "../conexiones/storage.h"
#include "memoria_lru.h"
#include "memoria_clockm.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

// ============================================================================
// WORKER - memoria_interna.c
// PASO A PASO GENERAL
// 1) Mantener memoria principal como array de marcos
// 2) Gestionar tablas de páginas por File:Tag
// 3) Resolver page-in / reemplazo (LRU o CLOCK-M)
// 4) Leer y escribir en memoria, logueando accesos y direcciones físicas
// 5) Flushear páginas dirty a Storage y liberar marcos
// 6) Registrar y consultar PC por Query para desalojos
// ============================================================================

extern int g_fd_storage;

// ---------------------------------------------------------------------------
// Estado interno
// ---------------------------------------------------------------------------
static void*    memoria_principal = NULL;
static uint32_t BLOCK_SIZE        = 0;
static uint32_t CANT_MARCOS       = 0;

static t_list* tablas_de_paginas  = NULL;
static t_list* marcos_fisicos     = NULL;
static pthread_mutex_t mutex_memoria;
static t_log* g_logger            = NULL;

typedef enum {
    ALGO_LRU,
    ALGO_CLOCKM
} t_algoritmo_reemplazo;

static t_algoritmo_reemplazo g_algoritmo = ALGO_LRU;

// Trackeo de PC por Query
typedef struct {
    uint32_t query_id;
    uint32_t pc;
} t_query_pc;

static t_list* g_queries_pc = NULL;

// ---------------------------------------------------------------------------
// Helpers generales
// ---------------------------------------------------------------------------
static unsigned long _now_ticks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000000ul + (unsigned long)ts.tv_nsec;
}

static int _strcmp_nullsafe(const char* a, const char* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcmp(a, b);
}

// ---------------------------------------------------------------------------
// Tablas de páginas
// ---------------------------------------------------------------------------
static t_tabla_paginas* _get_or_create_tabla(file_tag_t ft, int create) {
    if (!tablas_de_paginas) return NULL;

    for (int i = 0; i < list_size(tablas_de_paginas); i++) {
        t_tabla_paginas* tp = list_get(tablas_de_paginas, i);
        if (_strcmp_nullsafe(tp->ft.file, ft.file) == 0 &&
            _strcmp_nullsafe(tp->ft.tag,  ft.tag)  == 0) {
            return tp;
        }
    }

    if (!create) return NULL;

    t_tabla_paginas* tp = malloc(sizeof(*tp));
    tp->ft.file = strdup(ft.file ? ft.file : "");
    tp->ft.tag  = strdup(ft.tag  ? ft.tag  : "");
    tp->entradas = list_create();
    list_add(tablas_de_paginas, tp);

    return tp;
}

static t_etp* _buscar_etp_por_pagina(t_tabla_paginas* tp, uint32_t nro_pagina) {
    for (int i = 0; i < list_size(tp->entradas); i++) {
        t_etp* e = list_get(tp->entradas, i);
        if (e->nro_pagina == nro_pagina) return e;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Selección de marcos
// ---------------------------------------------------------------------------
static int _marco_libre(void) {
    for (int i = 0; i < (int)CANT_MARCOS; i++) {
        t_marco* m = list_get(marcos_fisicos, i);
        if (m->ocupado == 0) return (int)m->nro_marco;
    }
    return -1;
}

static int _elegir_marco_victima(void) {
    if (g_algoritmo == ALGO_LRU) {
        return memoria_lru_seleccionar_marco(marcos_fisicos, CANT_MARCOS);
    } else {
        return memoria_clockm_seleccionar_marco(marcos_fisicos, CANT_MARCOS);
    }
}

// ---------------------------------------------------------------------------
// Page-in y manejo de reemplazos
// ---------------------------------------------------------------------------
static int _pagein_etp(t_etp* etp, int fd_storage) {
    int marco = _marco_libre();
    int hubo_reemplazo = 0;

    if (marco < 0) {
        marco = _elegir_marco_victima();
        if (marco < 0) {
            if (g_logger) log_error(g_logger, "[MEM] Sin marcos y no se pudo elegir víctima.");
            return 0;
        }
        hubo_reemplazo = 1;

        t_marco* m_vict = list_get(marcos_fisicos, marco);
        t_etp*  vict   = m_vict->etp_asociada;

        if (vict) {
            // Flush si está dirty
            if (vict->presencia && vict->dirty) {
                char* src = (char*)memoria_principal + ((size_t)vict->nro_marco * BLOCK_SIZE);
                log_warning(g_logger,
                "[DBG] victim_flush(q=%u) %s:%s pag=%u blk=%u dirty=%u",
                etp->query_id,
                vict->ft.file ? vict->ft.file : "",
                vict->ft.tag  ? vict->ft.tag  : "",
                vict->nro_pagina,
                vict->id_bloque_storage,
                vict->dirty);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

                if (storage_io_write_block(
                        vict->ft,
                        vict->id_bloque_storage,
                        src,
                        BLOCK_SIZE,
                        fd_storage,
                        g_logger) != 1) {
                    if (g_logger) {
                        log_error(
                            g_logger,
                            "[MEM] Error flusheando página víctima."
                        );
                    }
                    return 0;
                }
                vict->dirty = 0;
            }

            // Log de liberación de marco de la víctima
            if (g_logger && vict->presencia) {
                log_info(
                    g_logger,
                    "Query %u: Se libera el Marco: %d perteneciente al - File: %s - Tag: %s",
                    (unsigned)vict->query_id,
                    marco,
                    vict->ft.file ? vict->ft.file : "",
                    vict->ft.tag  ? vict->ft.tag  : ""
                );
            }

            // Log de reemplazo de página
            if (g_logger) {
                log_info(
                    g_logger,
                    "## Query %u: Se reemplaza la página %s:%s/%u por la %s:%s/%u",
                    (unsigned)etp->query_id,
                    vict->ft.file ? vict->ft.file : "",
                    vict->ft.tag  ? vict->ft.tag  : "",
                    vict->nro_pagina,
                    etp->ft.file  ? etp->ft.file  : "",
                    etp->ft.tag   ? etp->ft.tag   : "",
                    etp->nro_pagina
                );
            }

            vict->presencia  = 0;
            vict->nro_marco  = 0;
            vict->ultimo_uso = 0;
        }

        m_vict->ocupado      = 0;
        m_vict->etp_asociada = NULL;

        if (g_algoritmo == ALGO_CLOCKM) {
            memoria_clockm_limpiar_referencia(marco);
        }
    }

    // Leer bloque desde Storage
    char* dst = (char*)memoria_principal + ((size_t)marco * BLOCK_SIZE);
    if (storage_io_read_block(
            etp->ft,
            etp->id_bloque_storage,
            fd_storage,
            g_logger,
            dst,
            BLOCK_SIZE) != 1) {
        if (g_logger) {
            log_error(g_logger, "[MEM] Error leyendo bloque desde Storage.");
        }
        return 0;
    }

    t_marco* m = list_get(marcos_fisicos, marco);
    m->ocupado      = 1;
    m->etp_asociada = etp;

    etp->nro_marco   = (uint32_t)marco;
    etp->presencia   = 1;
    etp->dirty       = 0;
    etp->ultimo_uso  = _now_ticks();

    if (g_algoritmo == ALGO_CLOCKM) {
        memoria_clockm_marcar_referencia(marco);
    }

    // Agregar el log de asignación de marco aquí
    if (g_logger) {
        log_info(
            g_logger,
            "Query %u: Se asigna el Marco: %d a la Página: %u perteneciente al - File: %s - Tag: %s",
            (unsigned)etp->query_id,
            marco,
            etp->nro_pagina,
            etp->ft.file ? etp->ft.file : "",
            etp->ft.tag  ? etp->ft.tag  : ""
        );
    }

    // Log genérico del PageIn (solo informativo)
    if (g_logger) {
        const char* algo = (g_algoritmo == ALGO_LRU) ? "LRU" : "CLOCK-M";
        if (hubo_reemplazo) {
            log_info(
                g_logger,
                "[MEM] PageIn(%s) REEMPLAZO -> pag=%u -> marco=%d",
                algo,
                etp->nro_pagina,
                marco
            );
        } else {
            log_info(
                g_logger,
                "[MEM] PageIn(%s) -> pag=%u -> marco=%d",
                algo,
                etp->nro_pagina,
                marco
            );
        }
    }

    return 1;
}

static t_etp* _get_etp_y_asegurar_presencia(
    file_tag_t ft,
    uint32_t   dir_base,
    int        query_id,
    int        fd_storage
) {
    uint32_t nro_pagina = dir_base / BLOCK_SIZE;

    t_tabla_paginas* tp = _get_or_create_tabla(ft, 1);
    t_etp* etp = _buscar_etp_por_pagina(tp, nro_pagina);
    int es_nueva = 0;

    if (!etp) {
        etp = malloc(sizeof(*etp));
        etp->ft.file = strdup(ft.file ? ft.file : "");
        etp->ft.tag  = strdup(ft.tag ? ft.tag : "");
        etp->nro_pagina        = nro_pagina;
        etp->nro_marco         = 0;
        etp->id_bloque_storage = nro_pagina;
        etp->presencia         = 0;
        etp->dirty             = 0;
        etp->query_id          = (uint32_t)query_id;
        etp->ultimo_uso        = 0;
        list_add(tp->entradas, etp);
        es_nueva = 1;
    }

    etp->query_id = (uint32_t)query_id;///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    if (es_nueva || !etp->presencia) {
        if (g_logger) {
            log_info(
                g_logger,
                "Query %u: - Memoria Miss - File: %s - Tag: %s - Pagina: %u",
                (unsigned)query_id,
                ft.file ? ft.file : "",
                ft.tag  ? ft.tag  : "",
                nro_pagina
            );
        }

        if (!_pagein_etp(etp, fd_storage)) {
            return NULL;
        }
    }

    etp->ultimo_uso = _now_ticks();

    if (g_algoritmo == ALGO_CLOCKM && etp->presencia) {
        memoria_clockm_marcar_referencia((int)etp->nro_marco);
    }

    return etp;
}

// ---------------------------------------------------------------------------
// Init / Destroy
// ---------------------------------------------------------------------------
static void _etp_free(void* p) {
    t_etp* e = (t_etp*)p;
    free(e->ft.file);
    free(e->ft.tag);
    free(e);
}

static void _tabla_free(void* p) {
    t_tabla_paginas* tp = (t_tabla_paginas*)p;
    if (tp->entradas) list_destroy_and_destroy_elements(tp->entradas, _etp_free);
    free(tp->ft.file);
    free(tp->ft.tag);
    free(tp);
}

static void _qpc_free(void* p) {
    free((t_query_pc*)p);
}

int memoria_init(
    uint32_t    tam_memoria,
    uint32_t    block_size,
    const char* algoritmo,
    t_log*      logger
) {
    g_logger   = logger;
    BLOCK_SIZE = block_size;

    if (!algoritmo) {
        g_algoritmo = ALGO_LRU;
    } else if (strcmp(algoritmo, "LRU") == 0) {
        g_algoritmo = ALGO_LRU;
    } else if (strcmp(algoritmo, "CLOCK-M") == 0 ||
               strcmp(algoritmo, "CLOCKM")  == 0 ||
               strcmp(algoritmo, "CLOCK")   == 0) {
        g_algoritmo = ALGO_CLOCKM;
    } else {
        g_algoritmo = ALGO_LRU;
        if (logger) log_warning(logger, "[MEM] Algoritmo '%s' desconocido, usando LRU.", algoritmo);
    }

    if (pthread_mutex_init(&mutex_memoria, NULL) != 0) {
        if (logger) log_error(logger, "[MEM] No se pudo inicializar mutex.");
        return 0;
    }

    memoria_principal = malloc(tam_memoria);
    if (!memoria_principal) {
        if (logger) log_error(logger, "[MEM] No se pudo reservar memoria principal.");
        return 0;
    }
    memset(memoria_principal, 0, tam_memoria);

    CANT_MARCOS       = tam_memoria / block_size;
    tablas_de_paginas = list_create();
    marcos_fisicos    = list_create();
    g_queries_pc      = list_create();

    for (uint32_t i = 0; i < CANT_MARCOS; i++) {
        t_marco* m   = malloc(sizeof(*m));
        m->nro_marco = i;
        m->ocupado   = 0;
        m->etp_asociada = NULL;
        list_add(marcos_fisicos, m);
    }

    if (g_algoritmo == ALGO_CLOCKM) {
        memoria_clockm_init(CANT_MARCOS);
    }

    if (logger) {
        const char* nombre_algo = (g_algoritmo == ALGO_LRU) ? "LRU" : "CLOCK-M";
        log_info(
            logger,
            "[MEM] Init OK: %u bytes, %u marcos x %u bytes. Algoritmo=%s",
            tam_memoria,
            CANT_MARCOS,
            BLOCK_SIZE,
            nombre_algo
        );
    }

    return 1;
}

uint32_t memoria_get_block_size(void) {
    return BLOCK_SIZE;
}

void memoria_destroy(void) {
    if (marcos_fisicos) {
        list_destroy_and_destroy_elements(marcos_fisicos, free);
        marcos_fisicos = NULL;
    }
    if (tablas_de_paginas) {
        list_destroy_and_destroy_elements(tablas_de_paginas, _tabla_free);
        tablas_de_paginas = NULL;
    }
    if (g_queries_pc) {
        list_destroy_and_destroy_elements(g_queries_pc, _qpc_free);
        g_queries_pc = NULL;
    }
    if (memoria_principal) {
        free(memoria_principal);
        memoria_principal = NULL;
    }
    pthread_mutex_destroy(&mutex_memoria);

    if (g_logger) log_info(g_logger, "[MEM] Destroy OK.");
}

// ---------------------------------------------------------------------------
// READ / WRITE / FLUSH
// ---------------------------------------------------------------------------
int memoria_escribir(
    file_tag_t  ft,
    uint32_t    dir_base,
    uint32_t    size,
    const char* content,
    int         query_id,
    int         fd_storage,
    t_log*      logger,
    uint32_t    retardo_ms
) {
    pthread_mutex_lock(&mutex_memoria);

    uint32_t remaining = size;
    uint32_t cur_dir   = dir_base;
    const char* src_ptr = content;

    while (remaining > 0) {

        // Traer/asegurar la página correspondiente a cur_dir
        t_etp* etp = _get_etp_y_asegurar_presencia(ft, cur_dir, query_id, fd_storage);
        if (!etp) {
            pthread_mutex_unlock(&mutex_memoria);
            return -1;
        }

        uint32_t off = cur_dir % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - off;
        if (chunk > remaining) chunk = remaining;

        char* dst = (char*)memoria_principal + ((size_t)etp->nro_marco * BLOCK_SIZE) + off;
        memcpy(dst, src_ptr, chunk);

        // Marcar dirty por página
        etp->dirty      = 1;
        etp->ultimo_uso = _now_ticks();

        uint32_t dir_fisica = etp->nro_marco * BLOCK_SIZE + off;

        // Retardo por acceso (si cruzás páginas, hay múltiples accesos reales)
        usleep(retardo_ms * 1000);

        // Log (por chunk, con dirección física real)
        if (logger) {
            char valor_str[64];
            uint32_t log_len = chunk < (sizeof(valor_str) - 1) ? chunk : (uint32_t)(sizeof(valor_str) - 1);
            memcpy(valor_str, src_ptr, log_len);
            valor_str[log_len] = '\0';

            log_info(
                logger,
                "Query %u: Acción: ESCRIBIR - Dirección Física: %u - Valor: %s",
                (unsigned)query_id,
                dir_fisica,
                valor_str
            );
        }

        // Avanzar
        cur_dir   += chunk;
        src_ptr   += chunk;
        remaining -= chunk;
    }

    pthread_mutex_unlock(&mutex_memoria);
    return 1;
}

int memoria_leer(
    file_tag_t ft,
    uint32_t   dir_base,
    uint32_t   size,
    char*      destino,
    int        query_id,
    int        fd_storage,
    t_log*     logger,
    uint32_t   retardo_ms
) {
    pthread_mutex_lock(&mutex_memoria);

    uint32_t remaining = size;
    uint32_t cur_dir   = dir_base;
    char* dst_ptr      = destino;

    while (remaining > 0) {

        t_etp* etp = _get_etp_y_asegurar_presencia(ft, cur_dir, query_id, fd_storage);
        if (!etp) {
            pthread_mutex_unlock(&mutex_memoria);
            return -1;
        }

        uint32_t off = cur_dir % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - off;
        if (chunk > remaining) chunk = remaining;

        char* src = (char*)memoria_principal + ((size_t)etp->nro_marco * BLOCK_SIZE) + off;
        memcpy(dst_ptr, src, chunk);

        etp->ultimo_uso = _now_ticks();

        uint32_t dir_fisica = etp->nro_marco * BLOCK_SIZE + off;

        usleep(retardo_ms * 1000);

        if (logger) {
            char valor_str[64];
            uint32_t log_len = chunk < (sizeof(valor_str) - 1) ? chunk : (uint32_t)(sizeof(valor_str) - 1);
            memcpy(valor_str, dst_ptr, log_len);
            valor_str[log_len] = '\0';

            log_info(
                logger,
                "Query %u: Acción: LEER - Dirección Física: %u - Valor: %s",
                (unsigned)query_id,
                dir_fisica,
                valor_str
            );
        }

        cur_dir   += chunk;
        dst_ptr   += chunk;
        remaining -= chunk;
    }

    pthread_mutex_unlock(&mutex_memoria);
    return 1;
}

int memoria_flush(
    file_tag_t ft,
    int        fd_storage,
    t_log*     logger
) {
    pthread_mutex_lock(&mutex_memoria);

    t_tabla_paginas* tp = _get_or_create_tabla(ft, 0);
    int flushed = 0;
    int hubo_error = 0;

    if (tp) {
        for (int i = 0; i < list_size(tp->entradas); i++) {
            t_etp* etp = list_get(tp->entradas, i);

            if (etp->presencia && etp->dirty) {

                char* src = (char*)memoria_principal + ((size_t)etp->nro_marco * BLOCK_SIZE);
                log_warning(logger,
                "[DBG] flush_explicit %s:%s pag=%u blk=%u dirty=%u",
                etp->ft.file ? etp->ft.file : "",
                etp->ft.tag  ? etp->ft.tag  : "",////////////////////////////////////////////////////////////////////////////////////////////////////////
                etp->nro_pagina,
                etp->id_bloque_storage,
                etp->dirty);


                int ok = storage_io_write_block(
                    etp->ft,
                    etp->id_bloque_storage,
                    src,
                    BLOCK_SIZE,
                    fd_storage,
                    logger
                );

                if (ok == 1) {
                    etp->dirty = 0;
                    flushed++;
                } else {
                    hubo_error = 1;

                    if (logger) {
                        log_error(
                            logger,
                            "[MEM] Error flusheando página (File=%s Tag=%s Pag=%u).",
                            etp->ft.file,
                            etp->ft.tag,
                            etp->nro_pagina
                        );
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&mutex_memoria);

    // Si hubo algún error, informarlo al caller
    if (hubo_error)
        return -1;

    // Si todo bien, devolver cantidad de páginas flusheadas
    return flushed;
}

// ---------------------------------------------------------------------------
// Flushes especiales
// ---------------------------------------------------------------------------
void memoria_flush_global(void) {
    pthread_mutex_lock(&mutex_memoria);

    int fd_storage = g_fd_storage;

    for (int i = 0; i < list_size(tablas_de_paginas); i++) {
        t_tabla_paginas* tp = list_get(tablas_de_paginas, i);
        for (int j = 0; j < list_size(tp->entradas); j++) {
            t_etp* etp = list_get(tp->entradas, j);
            if (!etp->presencia) continue;

            if (etp->dirty && fd_storage >= 0) {
                char* src = (char*)memoria_principal + ((size_t)etp->nro_marco * BLOCK_SIZE);
                storage_io_write_block(
                    etp->ft,
                    etp->id_bloque_storage,
                    src,
                    BLOCK_SIZE,
                    fd_storage,
                    g_logger
                );
                etp->dirty = 0;
            }

            t_marco* m = list_get(marcos_fisicos, (int)etp->nro_marco);

            if (g_logger) {
                log_info(
                    g_logger,
                    "Query %u: Se libera el Marco: %u perteneciente al - File: %s - Tag: %s",
                    (unsigned)etp->query_id,
                    (unsigned)m->nro_marco,
                    etp->ft.file ? etp->ft.file : "",
                    etp->ft.tag  ? etp->ft.tag  : ""
                );
            }

            m->ocupado      = 0;
            m->etp_asociada = NULL;

            if (g_algoritmo == ALGO_CLOCKM) {
                memoria_clockm_limpiar_referencia((int)m->nro_marco);
            }

            etp->presencia  = 0;
            etp->nro_marco  = 0;
            etp->ultimo_uso = 0;
        }
    }

    pthread_mutex_unlock(&mutex_memoria);

    if (g_logger) log_info(g_logger, "[MEM] Flush global completo (todas las páginas liberadas).");
    else fprintf(stderr, "[MEM] Flush global completo.\n");
}

void memoria_flush_implicito(uint32_t query_id) {
    pthread_mutex_lock(&mutex_memoria);

    int fd_storage = g_fd_storage;

    for (int i = 0; i < list_size(tablas_de_paginas); i++) {
        t_tabla_paginas* tp = list_get(tablas_de_paginas, i);
        for (int j = 0; j < list_size(tp->entradas); j++) {
            t_etp* etp = list_get(tp->entradas, j);
            if (etp->query_id != query_id) continue;
            if (!etp->presencia) continue;

            log_warning(g_logger,
            "[DBG] flush_implicito(q=%u) -> etp q_owner=%u %s:%s pag=%u blk=%u dirty=%u",
            query_id, etp->query_id,
            etp->ft.file, etp->ft.tag,
            etp->nro_pagina, etp->id_bloque_storage,
            etp->dirty);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


            if (etp->dirty && fd_storage >= 0) {
                char* src = (char*)memoria_principal + ((size_t)etp->nro_marco * BLOCK_SIZE);
                storage_io_write_block(
                    etp->ft,
                    etp->id_bloque_storage,
                    src,
                    BLOCK_SIZE,
                    fd_storage,
                    g_logger
                );
                etp->dirty = 0;
            }

            t_marco* m = list_get(marcos_fisicos, (int)etp->nro_marco);

            if (g_logger) {
                log_info(
                    g_logger,
                    "Query %u: Se libera el Marco: %u perteneciente al - File: %s - Tag: %s",
                    (unsigned)etp->query_id,
                    (unsigned)m->nro_marco,
                    etp->ft.file ? etp->ft.file : "",
                    etp->ft.tag  ? etp->ft.tag  : ""
                );
            }

            m->ocupado      = 0;
            m->etp_asociada = NULL;

            if (g_algoritmo == ALGO_CLOCKM) {
                memoria_clockm_limpiar_referencia((int)m->nro_marco);
            }

            etp->presencia  = 0;
            etp->nro_marco  = 0;
            etp->ultimo_uso = 0;
        }
    }

    if (g_queries_pc) {
        for (int i = 0; i < list_size(g_queries_pc); i++) {
            t_query_pc* q = list_get(g_queries_pc, i);
            if (q->query_id == query_id) {
                list_remove_and_destroy_element(g_queries_pc, i, _qpc_free);
                break;
            }
        }
    }

    pthread_mutex_unlock(&mutex_memoria);

    if (g_logger) {
        log_info(
            g_logger,
            "[MEM] Flush implícito de Q=%u completo (marcos liberados).",
            query_id
        );
    } else {
        fprintf(stderr, "[MEM] Flush implícito de Q=%u completo.\n", query_id);
    }
}

// NUEVO: liberar marcos de una Query SIN PERSISTIR (END normal)
// - No escribe a Storage, incluso si hay páginas dirty.
// - Libera marcos y descarta cambios (dirty=0).
void memoria_liberar_implicito(uint32_t query_id) {
    pthread_mutex_lock(&mutex_memoria);

    for (int i = 0; i < list_size(tablas_de_paginas); i++) {
        t_tabla_paginas* tp = list_get(tablas_de_paginas, i);
        for (int j = 0; j < list_size(tp->entradas); j++) {
            t_etp* etp = list_get(tp->entradas, j);
            if (etp->query_id != query_id) continue;
            if (!etp->presencia) continue;

            // NO PERSISTE: descartamos dirty si existiera
            etp->dirty = 0;

            t_marco* m = list_get(marcos_fisicos, (int)etp->nro_marco);

            if (g_logger) {
                log_info(
                    g_logger,
                    "Query %u: Se libera el Marco: %u perteneciente al - File: %s - Tag: %s",
                    (unsigned)etp->query_id,
                    (unsigned)m->nro_marco,
                    etp->ft.file ? etp->ft.file : "",
                    etp->ft.tag  ? etp->ft.tag  : ""
                );
            }

            m->ocupado      = 0;
            m->etp_asociada = NULL;

            if (g_algoritmo == ALGO_CLOCKM) {
                memoria_clockm_limpiar_referencia((int)m->nro_marco);
            }

            etp->presencia  = 0;
            etp->nro_marco  = 0;
            etp->ultimo_uso = 0;
        }
    }

    if (g_queries_pc) {
        for (int i = 0; i < list_size(g_queries_pc); i++) {
            t_query_pc* q = list_get(g_queries_pc, i);
            if (q->query_id == query_id) {
                list_remove_and_destroy_element(g_queries_pc, i, _qpc_free);
                break;
            }
        }
    }

    pthread_mutex_unlock(&mutex_memoria);

    if (g_logger) {
        log_info(
            g_logger,
            "[MEM] Liberación implícita de Q=%u completa (sin persistir).",
            query_id
        );
    } else {
        fprintf(stderr, "[MEM] Liberación implícita de Q=%u completa.\n", query_id);
    }
}

// ---------------------------------------------------------------------------
// PC por Query
// ---------------------------------------------------------------------------
static t_query_pc* _buscar_qpc(uint32_t query_id) {
    if (!g_queries_pc) return NULL;
    for (int i = 0; i < list_size(g_queries_pc); i++) {
        t_query_pc* q = list_get(g_queries_pc, i);
        if (q->query_id == query_id) return q;
    }
    return NULL;
}

void memoria_registrar_pc(uint32_t query_id, uint32_t pc) {
    pthread_mutex_lock(&mutex_memoria);

    if (!g_queries_pc)
        g_queries_pc = list_create();

    t_query_pc* q = _buscar_qpc(query_id);
    if (!q) {
        q = malloc(sizeof(*q));
        q->query_id = query_id;
        q->pc       = pc;
        list_add(g_queries_pc, q);
    } else {
        q->pc = pc;
    }

    pthread_mutex_unlock(&mutex_memoria);
}

uint32_t query_pc_actual(uint32_t query_id) {
    pthread_mutex_lock(&mutex_memoria);

    uint32_t pc = 0;
    t_query_pc* q = _buscar_qpc(query_id);
    if (q) pc = q->pc;

    pthread_mutex_unlock(&mutex_memoria);
    return pc;
}
