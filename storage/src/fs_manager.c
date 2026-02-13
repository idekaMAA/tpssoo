// ============================================================================
// fs_manager.c (VERSIÓN 2) - COMPLETA, CON CAMBIOS PARA "QUERY_ID" EN READ
// NO SE ELIMINA NADA: lo viejo queda preservado en #if 0
// ============================================================================

#include "fs_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <commons/log.h>
#include <commons/bitarray.h>
#include <commons/string.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include "hash_utils.h"
#include <errno.h>
#include <limits.h>
#include "blocks_hash_index.h"
#include "storage_utils.h"
#include "inicializacion.h"

extern t_superbloque g_superbloque;
extern storage_config_t g_storage_config;
extern void* g_blocks_dat_map;
extern t_bitarray* g_bitmap;

static void _free_char(char *s) {
    free(s);
}

// --- forward declarations (EVITA implicit declaration / conflicting types) ---
static off_t _block_offset(uint32_t block_id);
static bool  _list_get_u32(t_list* lista, uint32_t idx, uint32_t* out);

// === Helpers de Rutas y Parsing ===

bool fs_parsear_file_tag(const char* file_tag_str, char** file_out, char** tag_out) {
    if (!file_tag_str) return false;
    const char* sep = strchr(file_tag_str, ':');
    if (!sep) {
        return false;
    }
    size_t len_file = sep - file_tag_str;
    if (len_file == 0) return false;

    *file_out = strndup(file_tag_str, len_file);
    *tag_out  = strdup(sep + 1);
    if (!*file_out || !*tag_out) {
        if (*file_out) free(*file_out);
        if (*tag_out)  free(*tag_out);
        *file_out = NULL; *tag_out = NULL;
        return false;
    }
    return true;
}

bool fs_existe_file_tag(const char* punto_montaje, const char* file_name, const char* tag_name) {
    char* ruta_completa = string_from_format("%s/files/%s/%s", punto_montaje, file_name, tag_name);

    struct stat st;
    int resultado = stat(ruta_completa, &st);
    free(ruta_completa);

    return (resultado == 0 && S_ISDIR(st.st_mode));
}

char* fs_obtener_ruta_tag(const char* punto_montaje, const char* file_name, const char* tag_name, const char* sub_path) {
    char* ruta_base = string_from_format("%s/files/%s/%s", punto_montaje, file_name, tag_name);
    if (sub_path) {
        char* ruta_final = string_from_format("%s/%s", ruta_base, sub_path);
        free(ruta_base);
        return ruta_final;
    }
    return ruta_base;
}


// === Gestión de Bloques (Bitmap) ===
// Ahora usa bitmap_reservar_nuevo_bloque / bitmap_liberar_bloque y loguea con el formato obligatorio.

bool fs_asignar_bloque_libre(uint32_t query_id, uint32_t* bloque_fisico_idx) {
    if (!bloque_fisico_idx) return false;

    int32_t elegido = bitmap_reservar_nuevo_bloque();
    if (elegido < 0) {
        log_error(g_storage_config.logger, "Error: Espacio Insuficiente. Todos los bloques están ocupados.");
        return false;
    }

    *bloque_fisico_idx = (uint32_t)elegido;

    // Log obligatorio: Bloque Físico Reservado
    log_info(g_storage_config.logger,
             "##Query: %u - Bloque Físico Reservado - Número de Bloque: %u",
             query_id,
             *bloque_fisico_idx);

    return true;
}

bool fs_liberar_bloque(uint32_t query_id, uint32_t bloque_fisico_idx){
    // Usamos bitmap_liberar_bloque (thread-safe) y log obligatorio
    bitmap_liberar_bloque(bloque_fisico_idx);

    log_info(g_storage_config.logger, "##Query: %u - Bloque Físico Liberado - Número de Bloque: %u", query_id, bloque_fisico_idx);
    return true;
}


static uint32_t fs_obtener_bloque_fisico_de_metadata(const char* file_tag_name, uint32_t bloque_logico_idx){
    char* file_name = NULL;
    char* tag_name = NULL;
    char* ruta_metadata  = NULL;
    t_config* metadata   = NULL;
    uint32_t bloque_fisico_idx = UINT32_MAX;

    if (!fs_parsear_file_tag(file_tag_name, &file_name, &tag_name)) {
        log_error(g_storage_config.logger, "fs_obtener_bloque_fisico_de_metadata: formato inválido '%s'", file_tag_name);
        goto cleanup;
    }

    char* ruta_check = string_from_format("%s/files/%s/%s", g_storage_config.punto_montaje, file_name, tag_name);
    if (!ruta_check) {
        log_error(g_storage_config.logger, "fs_obtener_bloque_fisico_de_metadata: OOM armando ruta_check");
        goto cleanup;
    }
    struct stat st;
    if (stat(ruta_check, &st) != 0 || !S_ISDIR(st.st_mode)) {
        log_error(g_storage_config.logger, "Error: File/Tag inexistente: %s/%s", file_name, tag_name);
        free(ruta_check);
        goto cleanup;
    }
    free(ruta_check);

    // Construyo la ruta a metadata usando la convención /files/FILE/TAG/metadata.config
    ruta_metadata = string_from_format("%s/files/%s/%s/metadata.config", g_storage_config.punto_montaje, file_name, tag_name);
    if (!ruta_metadata) {
        log_error(g_storage_config.logger, "fs_obtener_bloque_fisico_de_metadata: OOM armando ruta_metadata");
        goto cleanup;
    }
    metadata = config_create(ruta_metadata);
    if (metadata == NULL) {
        log_error(g_storage_config.logger, "Error: metadata.config no encontrado para %s.", file_tag_name);
        goto cleanup;
    }

    if (!config_has_property(metadata, "BLOCKS")) {
        log_error(g_storage_config.logger, "Error: metadata.config de %s no tiene la clave BLOCKS.", file_tag_name);
        goto cleanup;
    }

    char** array_bloques = config_get_array_value(metadata, "BLOCKS");
    if (!array_bloques) {
        log_error(g_storage_config.logger, "Error: config_get_array_value devolvió NULL para %s", ruta_metadata);
        goto cleanup;
    }

    int indice_requerido = (int)bloque_logico_idx;
    int cant_bloques     = string_array_size(array_bloques);
    if (indice_requerido >= cant_bloques) {
        log_error(g_storage_config.logger, "Error: Acceso fuera de rango. Bloque lógico %u no asignado en metadata (size=%d).", bloque_logico_idx, cant_bloques);
        string_array_destroy(array_bloques);
        goto cleanup;
    }

    const char* valor_str = array_bloques[indice_requerido];
    errno = 0;
    unsigned long v = strtoul(valor_str, NULL, 10);
    if (errno == ERANGE) {
        log_error(g_storage_config.logger, "Error: Bloque físico de metadata inválido: %s", valor_str);
        bloque_fisico_idx = UINT32_MAX;
    } else {
        bloque_fisico_idx = (uint32_t)v;
        log_info(g_storage_config.logger, "HASH_DEBUG: fs_obtener_bloque_fisico_de_metadata(%s, log_idx=%u) -> block_fisico=%u", file_tag_name, bloque_logico_idx, bloque_fisico_idx);
    }

    string_array_destroy(array_bloques);

cleanup:
    if (metadata)      config_destroy(metadata);
    if (ruta_metadata) free(ruta_metadata);
    if (file_name)     free(file_name);
    if (tag_name)      free(tag_name);

    if (bloque_fisico_idx == UINT32_MAX) {
        log_warning(g_storage_config.logger, "HASH_DEBUG: fs_obtener_bloque_fisico_de_metadata(%s, %u) -> INVALIDO", file_tag_name, bloque_logico_idx);
    }
    return bloque_fisico_idx;
}


// === Operaciones Principales ===

// Ahora recibe query_id para poder loguear con el formato obligatorio.
bool fs_crear_file_tag(uint32_t query_id, const char* punto_montaje, const char* file_tag_name) {
    if (!punto_montaje || !file_tag_name) return false;

    // Validamos formato FILE:TAG
    char* file = NULL;
    char* tag  = NULL;
    if (!fs_parsear_file_tag(file_tag_name, &file, &tag)) {
        log_error(g_storage_config.logger, "CREATE: formato inválido '%s' (esperado FILE:TAG).", file_tag_name);
        return false;
    }

    char* dir = string_from_format("%s/files/%s/%s", punto_montaje, file, tag);
    if (!dir) { free(file); free(tag); return false; }

    // Crear directorio recursivo (padres)
    if (!crear_directorio_rescursivo(dir)) {
        log_error(g_storage_config.logger, "CREATE: no pude crear directorio %s", dir);
        free(file); free(tag); free(dir);
        return false;
    }

    // Crear metadata.config inicial
    char* meta_path = string_from_format("%s/metadata.config", dir);
    if (!meta_path) { free(file); free(tag); free(dir); return false; }

    // Si el archivo ya existe, considero que el CREATE falla
    if (access(meta_path, F_OK) == 0) {
        log_error(g_storage_config.logger, "CREATE: ya existe %s", meta_path);
        free(file); free(tag); free(dir); free(meta_path);
        return false;
    }

    // Crear y escribir metadata inicial
    FILE* f = fopen(meta_path, "w");
    if (!f) {
        log_error(g_storage_config.logger, "CREATE: no pude crear %s (%s)", meta_path, strerror(errno));
        free(file); free(tag); free(dir); free(meta_path);
        return false;
    }

    // Valores iniciales coherentes con meta_guardar / meta_cargar
    fprintf(f, "ESTADO=WORK_IN_PROGRESS\nBLOCKS=[]\nTAMANO=0\n");
    fclose(f);

    // Log obligatorio: File Creado
    log_info(g_storage_config.logger,
             "##Query: %u - File Creado %s",
             query_id,
             file_tag_name);

    free(file); free(tag); free(dir); free(meta_path);
    return true;
}


ssize_t fs_leer_bloque(uint32_t query_id,
                        const char* punto_montaje,
                        const char* file_tag_name,
                        uint32_t bloque_logico_idx,
                        void* buffer_salida,
                        uint32_t tam_a_leer) {

    (void)punto_montaje; // ya usamos g_storage_config.punto_montaje internamente

    uint32_t bloque_fisico_idx = fs_obtener_bloque_fisico_de_metadata(file_tag_name, bloque_logico_idx);

    if (bloque_fisico_idx == UINT32_MAX){
        return -1;
    }

    if (tam_a_leer > g_superbloque.tam_bloque_bytes){
        log_error(g_storage_config.logger, "Error: Lectura fuera de límite. Solicitado %u bytes.", tam_a_leer);
        return -1;
    }

    size_t offset = (size_t)bloque_fisico_idx * g_superbloque.tam_bloque_bytes;
    uint32_t bytes_a_copiar = tam_a_leer;

    if (g_blocks_dat_map == NULL) return -1;

    memcpy(buffer_salida, (char*)g_blocks_dat_map + offset, bytes_a_copiar);

    // Log obligatorio: Bloque Lógico Leído
    log_info(g_storage_config.logger,
             "##Query: %u - Bloque Lógico Leído %s - Número de Bloque: %u",
             query_id,
             file_tag_name,
             bloque_logico_idx);

    return (ssize_t)bytes_a_copiar;
}

/// helpers para metadata ///

static bool _meta_paths(const char* path_logico, char** out_dir, char** out_meta) {
    if (!path_logico || !out_dir || !out_meta) return false;

    char* file = NULL;
    char* tag  = NULL;

    if (!fs_parsear_file_tag(path_logico, &file, &tag)) {
        if (file) free(file);
        if (tag)  free(tag);
        return false;
    }

    char* dir = string_from_format("%s/files/%s/%s", g_storage_config.punto_montaje, file, tag);
    if (!dir) {
        free(file); free(tag);
        return false;
    }

    char* meta = string_from_format("%s/metadata.config", dir);
    if (!meta) {
        free(file); free(tag); free(dir);
        return false;
    }

    *out_dir = dir;
    *out_meta = meta;

    free(file);
    free(tag);
    return true;
}

static char* _bloques_to_string(t_list* bloques) {
    int n = bloques ? list_size(bloques) : 0;

    if (n == 0)
        return string_duplicate("[]");

    char* out = string_new();
    string_append(&out, "[");
    for (int i = 0; i < n; i++) {
        uint32_t* p = list_get(bloques, i);
        uint32_t v = p ? *p : 0;

        char* tmp = string_from_format("%u", v);
        string_append(&out, tmp);
        free(tmp);

        if (i < n - 1)
            string_append(&out, ",");
    }

    string_append(&out, "]");
    return out;
}

static t_list* _string_to_bloques(const char* s) {
    if (!s) return NULL;
    char* copy = string_from_format("%s", s);

    if (!copy) return NULL;
    string_trim(&copy);
    if (copy[0] == '[') copy[0] = ' ';
    size_t n = strlen(copy);
    if (n > 0 && copy[n-1] == ']') copy[n-1] = ' ';
    char** parts = string_split(copy, ",");
    free(copy);

    t_list* bloques = list_create();
    if (!bloques) {
        for (char** it = parts; *it != NULL; ++it) {
            free(*it);
        }
        free(parts);
    }

    for (int i = 0; parts && parts[i]; ++i) {
        char* tok = parts[i];
        string_trim(&tok);
        parts[i] = tok;
        if (tok[0] == '\0') continue;
        char* endptr = NULL;
        unsigned long v = strtoul(tok, &endptr, 10);
        if (endptr == tok) continue;
        uint32_t* heap_u = malloc(sizeof(uint32_t));
        if (!heap_u) {
            for (char** it = parts + i; *it != NULL; ++it) {
                _free_char(*it);
            }
            for (int j = 0; j < list_size(bloques); ++j) {
                free(list_get(bloques, j));
            }
            list_destroy(bloques);
            free(parts);
            return NULL;
        }

        *heap_u = (uint32_t)v;
        list_add(bloques, heap_u);
    }
    for (int i = 0; parts && parts[i]; ++i) free(parts[i]);
    free(parts);

    return bloques;
}

void meta_liberar_lista_bloques(t_list* bloques) {
    if (!bloques) return;
    for (int i = 0; i < list_size(bloques); ++i) {
        void* p = list_get(bloques, i);
        if (p) {
            free(p);
        }
    }
    list_destroy(bloques);
}

bool meta_guardar(const char* path_logico, t_list* bloques, uint32_t tam_logico, const char* estado) {
    if (!path_logico) return false;
    char* dir = NULL;
    char* meta_path = NULL;
    if (!_meta_paths(path_logico, &dir, &meta_path)) return false;

    if (!crear_directorio_rescursivo(dir)) {
        log_error(g_storage_config.logger, "meta_guardar: no pude crear directorio %s", dir);
        free(dir); free(meta_path);
        return false;
    }

    char* s_bloques = _bloques_to_string(bloques);
    if (!s_bloques) {
        free(dir); free(meta_path);
        log_error(g_storage_config.logger, "meta_guardar: no pude serializar bloques");
        return false;
    }

    char s_tam[32];
    snprintf(s_tam, sizeof(s_tam), "%u", tam_logico);

    const char* s_estado = estado ? estado : "OK";

    FILE* f = fopen(meta_path, "w");
    if (!f) {
        log_error(g_storage_config.logger, "meta_guardar: no pude abrir %s para escritura: %s", meta_path, strerror(errno));
        free(s_bloques);
        free(dir); free(meta_path);
        return false;
    }

    int fprintf_res = fprintf(f, "BLOCKS=%s\nTAMANO=%s\nESTADO=%s\n", s_bloques, s_tam, s_estado);
    if (fprintf_res < 0) {
        log_error(g_storage_config.logger, "meta_guardar: error escribiendo en %s", meta_path);
        fclose(f);
        free(s_bloques);
        free(dir); free(meta_path);
        return false;
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    free(s_bloques);
    free(dir);
    free(meta_path);
    return true;
}


bool meta_cargar(const char* path_logico, t_list** out_bloques, uint32_t* out_tam_logico, char** out_estado) {
    if (!path_logico || !out_bloques || !out_tam_logico) return false;

    *out_bloques = NULL;
    *out_tam_logico = 0;
    if (out_estado) *out_estado = NULL;

    char* dir = NULL;
    char* meta_path = NULL;
    if (!_meta_paths(path_logico, &dir, &meta_path)) return false;

    t_config* cfg = config_create(meta_path);
    if (!cfg) {
        log_error(g_storage_config.logger, "meta_cargar: no existe %s", meta_path);
        free(dir); free(meta_path);
        return false;
    }

    if (!config_has_property(cfg, "BLOCKS") ||
        !config_has_property(cfg, "TAMANO") ||
        !config_has_property(cfg, "ESTADO")) {
        log_error(g_storage_config.logger, "meta_cargar: faltan claves en %s", meta_path);
        config_destroy(cfg); free(dir); free(meta_path);
        return false;
    }

    const char* s_blocks = config_get_string_value(cfg, "BLOCKS");
    const char* s_tam    = config_get_string_value(cfg, "TAMANO");
    const char* s_estado = config_get_string_value(cfg, "ESTADO");

    t_list* bloques = _string_to_bloques(s_blocks);
    if (!bloques) {
        log_error(g_storage_config.logger, "meta_cargar: parse de BLOCKS falló en %s", meta_path);
        config_destroy(cfg); free(dir); free(meta_path);
        return false;
    }

    char* endptr = NULL;
    unsigned long tam = strtoul(s_tam, &endptr, 10);
    if (endptr == s_tam) {
        log_error(g_storage_config.logger, "meta_cargar: TAMANO inválido en %s (%s)", meta_path, s_tam);
        meta_liberar_lista_bloques(bloques);
        config_destroy(cfg); free(dir); free(meta_path);
        return false;
    }

    *out_bloques = bloques;
    *out_tam_logico = (uint32_t)tam;

    if (out_estado) *out_estado = strdup(s_estado ? s_estado : "OK");

    config_destroy(cfg);
    free(dir);
    free(meta_path);
    return true;
}

static bool anadir_a_lista(t_list* lista, uint32_t v) {
    uint32_t* p = malloc(sizeof(uint32_t));
    if (!p) return false;
    *p = v;
    list_add(lista, p);
    return true;
}

static inline uint32_t bloques_para(uint32_t bytes, uint32_t block_size) {
    if (bytes == 0u) return 0u;
    return (bytes + block_size - 1u) / block_size;
}

bool path_logico_valido(const char* path) {
    if (!path) return false;
    if (path[0] == '\0') return false;
    if (path[0] == '/') return false;
    if (strstr(path, "..") != NULL) return false;
    return true;
}

bool fs_truncate(uint32_t query_id, const char* path_logico, uint32_t new_size) {
    if (!path_logico_valido(path_logico)) return false;

    t_list* bloques = NULL;
    uint32_t tam_actual = 0;
    char* estado = NULL;

    bool existia = meta_cargar(path_logico, &bloques, &tam_actual, &estado);
    if (!existia) {
        bloques = list_create();
        if (!bloques) return false;
        tam_actual = 0;
    }

    if (estado && strcmp(estado, "COMMITTED") == 0) {
        log_error(g_storage_config.logger,
                  "TRUNCATE prohibido: %s está COMMITTED. No se permite modificar tamaño.",
                  path_logico);
        meta_liberar_lista_bloques(bloques);
        free(estado);
        return false;
    }

    uint32_t bloques_actuales   = (uint32_t)list_size(bloques);
    uint32_t bloques_necesarios = bloques_para(new_size, g_storage_config.block_size);

    // CRECER
    if (bloques_necesarios > bloques_actuales) {
        uint32_t a_reservar = bloques_necesarios - bloques_actuales;

        for (uint32_t i = 0; i < a_reservar; ++i) {
            uint32_t b_idx;

            if (!fs_asignar_bloque_libre(query_id, &b_idx)) {
                // rollback de lo agregado en este truncate
                for (uint32_t j = 0; j < i; ++j) {
                    uint32_t* ultimo = (uint32_t*)list_remove(bloques, list_size(bloques) - 1);
                    if (ultimo) {
                        uint32_t b = *ultimo;

                        log_info(g_storage_config.logger,
                                 "##Query: %u - %s Se eliminó el hard link del bloque lógico %u al bloque físico %u",
                                 query_id, path_logico,
                                 (bloques_actuales + i - 1u - j), b);

                        // ref-- y si queda 0 liberar
                        if (hash_index_dec_ref_is_zero(b)) {
                            fs_liberar_bloque(query_id, b);
                            hash_index_remove_block(b);
                        }
                        free(ultimo);
                    }
                }
                if (estado) free(estado);
                return false;
            }

            if (!anadir_a_lista(bloques, b_idx)) {
                // no pudimos agregar a metadata → revertir este b_idx
                if (hash_index_dec_ref_is_zero(b_idx)) {
                    fs_liberar_bloque(query_id, b_idx);
                    hash_index_remove_block(b_idx);
                }
                if (estado) free(estado);
                meta_liberar_lista_bloques(bloques);
                return false;
            }

            // inicializar el bloque (cero) para evitar basura
            void* ptr = (char*)g_blocks_dat_map + (size_t)_block_offset(b_idx);
            memset(ptr, 0, g_storage_config.block_size);

            // refcount++ (ahora es referenciado por el archivo)
            hash_index_inc_ref(b_idx);

            // Log obligatorio: Hard Link Agregado
            uint32_t logical_idx = bloques_actuales + i;
            log_info(g_storage_config.logger,
                     "##Query: %u - %s Se agregó el hard link del bloque lógico %u al bloque físico %u",
                     query_id, path_logico, logical_idx, b_idx);
        }
    }

    // ACHICAR
    if (bloques_necesarios < bloques_actuales) {
        uint32_t a_liberar = bloques_actuales - bloques_necesarios;

        for (uint32_t i = 0; i < a_liberar; ++i) {
            uint32_t* ultimo = (uint32_t*)list_remove(bloques, list_size(bloques) - 1);
            if (!ultimo) break;

            uint32_t b = *ultimo;
            uint32_t logical_idx = bloques_necesarios + (a_liberar - 1u - i);

            log_info(g_storage_config.logger,
                     "##Query: %u - %s Se eliminó el hard link del bloque lógico %u al bloque físico %u",
                     query_id, path_logico, logical_idx, b);

            if (hash_index_dec_ref_is_zero(b)) {
                fs_liberar_bloque(query_id, b);
                hash_index_remove_block(b);
            }

            free(ultimo);
        }
    }

    bool ok = meta_guardar(path_logico, bloques, new_size, "OK");
    if (ok) {
        log_info(g_storage_config.logger,
                 "##Query: %u - File Truncado %s - Tamaño: %u",
                 query_id, path_logico, new_size);
    } else {
        log_error(g_storage_config.logger, "fs_truncate(%s): meta_guardar falló", path_logico);
    }

    meta_liberar_lista_bloques(bloques);
    if (estado) free(estado);
    return ok;
}


static bool _list_get_u32(t_list* lista, uint32_t idx, uint32_t* out) {
    if (!lista || !out) return false;
    if (idx >= (uint32_t)list_size(lista)) return false;
    uint32_t* p = list_get(lista, (int)idx);
    if (!p) return false;
    *out = *p;
    return true;
}

static off_t _block_offset(uint32_t block_id) {
    return (off_t)block_id * (off_t)g_storage_config.block_size;
}

// ---------------------------------------------------------------------------
// NUEVA FUNCIÓN (CON query_id) - PARA QUE "FUNCIONE BIEN LO DE LOS QUERYS"
// ---------------------------------------------------------------------------
bool fs_read_bloque(uint32_t query_id,
                    const char* path_logico,
                    uint32_t block_idx,
                    void* out_block,
                    uint32_t block_size)
{
    (void)block_size; // usamos g_storage_config.block_size como fuente de verdad

    if (!path_logico_valido(path_logico)) return false;
    if (!out_block) return false;
    if (!g_blocks_dat_map) return false;

    t_list* bloques = NULL;
    uint32_t tam_logico = 0;
    char* estado = NULL;

    bool ok = meta_cargar(path_logico, &bloques, &tam_logico, &estado);
    if (!ok) {
        if (estado) free(estado);
        return false;
    }

    uint32_t block_fisico = 0;
    ok = _list_get_u32(bloques, block_idx, &block_fisico);
    if (!ok) {
        meta_liberar_lista_bloques(bloques);
        if (estado) free(estado);
        return false;
    }

    off_t off_typed = _block_offset(block_fisico);
    size_t off = (size_t)off_typed;
    void* src = (char*)g_blocks_dat_map + off;
    memcpy(out_block, src, (size_t)g_storage_config.block_size);

    // Log obligatorio: Bloque Lógico Leído (con query_id)
    log_info(g_storage_config.logger,
             "##Query: %u - Bloque Lógico Leído %s - Número de Bloque: %u",
             query_id,
             path_logico,
             block_idx);

    meta_liberar_lista_bloques(bloques);
    if (estado) free(estado);
    return true;
}

bool fs_write_bloque(uint32_t query_id,
                     const char* path_logico,
                     uint32_t block_idx,
                     const void* data,
                     uint32_t len)
{
    if (len == 0) return true;
    if (!path_logico_valido(path_logico)) return false;
    if (!g_blocks_dat_map) return false;
    if (!data) return false;

    t_list*  bloques = NULL;
    uint32_t tam_logico = 0;
    char*    estado = NULL;

    if (!meta_cargar(path_logico, &bloques, &tam_logico, &estado)) {
        return false;
    }

    if (estado && strcmp(estado, "COMMITTED") == 0) {
        log_error(g_storage_config.logger, "WRITE prohibido: %s está COMMITTED", path_logico);
        free(estado);
        meta_liberar_lista_bloques(bloques);
        return false;
    }

    uint32_t old_block = 0;
    if (!_list_get_u32(bloques, block_idx, &old_block)) {
        log_error(g_storage_config.logger,
                  "WRITE: índice de bloque lógico inválido (%u) para %s",
                  block_idx, path_logico);
        if (estado) free(estado);
        meta_liberar_lista_bloques(bloques);
        return false;
    }

    const uint32_t bs = g_storage_config.block_size;

    // --- Construir bloque final (para write parcial correcto) ---
    void* final_block = malloc(bs);
    if (!final_block) {
        if (estado) free(estado);
        meta_liberar_lista_bloques(bloques);
        return false;
    }

    // Copio el contenido actual del bloque físico old_block
    void* old_ptr = (char*)g_blocks_dat_map + (size_t)_block_offset(old_block);
    memcpy(final_block, old_ptr, bs);

    // Aplico la escritura (asumo desde offset 0; si tenés offset, acá iría)
    if (len > bs) len = bs;
    memcpy(final_block, data, len);

    // --- Hash del contenido final ---
    char hash_hex[33];
    calcular_md5(final_block, bs, hash_hex);

    log_info(g_storage_config.logger,
             "HASH_DEBUG: fs_write_bloque path=%s log_idx=%u old_block=%u md5=%s",
             path_logico, block_idx, old_block, hash_hex);

    // --- 1) Si existe un bloque IDENTICO, deduplicar ---
    uint32_t existing_block = 0;
    if (hash_index_find_equal_block(hash_hex, final_block, bs, &existing_block)
        && existing_block != old_block)
    {
        log_info(g_storage_config.logger,
        "HASH_DEBUG: HIT dedup md5=%s -> existente=%u (yo tenia=%u)",
        hash_hex,
        existing_block,
        old_block
        );

        // Referenciar el existente
        hash_index_inc_ref(existing_block);

        // Actualizar metadata (bloque lógico -> existing_block)
        uint32_t* p = list_get(bloques, (int)block_idx);
        if (p) *p = existing_block;

        // Logs obligatorios hardlink
        log_info(g_storage_config.logger,
                 "##Query: %u - %s Se eliminó el hard link del bloque lógico %u al bloque físico %u",
                 query_id, path_logico, block_idx, old_block);

        log_info(g_storage_config.logger,
                 "##Query: %u - %s Se agregó el hard link del bloque lógico %u al bloque físico %u",
                 query_id, path_logico, block_idx, existing_block);

        log_info(g_storage_config.logger,
                 "##Query: %u - %s Bloque Lógico %u se reasigna de %u a %u",
                 query_id, path_logico, block_idx, old_block, existing_block);

        // Decrementar ref del viejo; si llega a 0 liberar bitmap y sacarlo del índice
        if (hash_index_dec_ref_is_zero(old_block)) {
            fs_liberar_bloque(query_id, old_block);
            hash_index_remove_block(old_block);
        }

        bool ok = meta_guardar(path_logico, bloques, tam_logico, estado ? estado : "OK");

        if (ok) {
            log_info(g_storage_config.logger,
                     "##Query: %u - Bloque Lógico Escrito %s - Número de Bloque: %u",
                     query_id, path_logico, block_idx);
        }

        free(final_block);
        if (estado) free(estado);
        meta_liberar_lista_bloques(bloques);
        return ok;
    }

    // --- 2) NO existe bloque igual: hay que escribir contenido final ---
    //     PERO: si old_block está compartido, no podés modificarlo (COW).
    //     Si está solo, podés escribirlo in-place.

    // ⚠️ Necesitás esta función o equivalente:
    // int refs = hash_index_get_ref(old_block);
    // Si tu índice no la tiene, te digo alternativa abajo.
    int refs = hash_index_get_ref(old_block);

    if (refs > 1) {
        // COW: reservar bloque nuevo y escribir ahí
        uint32_t new_block = 0;
        if (!fs_asignar_bloque_libre(query_id, &new_block)) {
            log_error(g_storage_config.logger, "WRITE: sin bloques libres para COW (%s)", path_logico);
            free(final_block);
            if (estado) free(estado);
            meta_liberar_lista_bloques(bloques);
            return false;
        }

        void* new_ptr = (char*)g_blocks_dat_map + (size_t)_block_offset(new_block);
        memcpy(new_ptr, final_block, bs);

        // Indexar y refcount del nuevo
        hash_index_register(new_block, hash_hex);
        hash_index_inc_ref(new_block);

        // Metadata: bloque lógico ahora apunta al nuevo
        uint32_t* p = list_get(bloques, (int)block_idx);
        if (p) *p = new_block;

        // Logs obligatorios hardlink
        log_info(g_storage_config.logger,
                 "##Query: %u - %s Se eliminó el hard link del bloque lógico %u al bloque físico %u",
                 query_id, path_logico, block_idx, old_block);

        log_info(g_storage_config.logger,
                 "##Query: %u - %s Se agregó el hard link del bloque lógico %u al bloque físico %u",
                 query_id, path_logico, block_idx, new_block);

        log_info(g_storage_config.logger,
                 "##Query: %u - %s Bloque Lógico %u se reasigna de %u a %u (COW)",
                 query_id, path_logico, block_idx, old_block, new_block);

        // El viejo pierde una referencia
        if (hash_index_dec_ref_is_zero(old_block)) {
            fs_liberar_bloque(query_id, old_block);
            hash_index_remove_block(old_block);
        }

    } else {
        // In-place: escribir sobre old_block
        void* dst = (char*)g_blocks_dat_map + (size_t)_block_offset(old_block);
        memcpy(dst, final_block, bs);

        // actualizar índice de hashes del bloque
        hash_index_remove_block(old_block);
        hash_index_register(old_block, hash_hex);
        // refcount no cambia (sigue siendo 1)
    }

    bool ok = meta_guardar(path_logico, bloques, tam_logico, estado ? estado : "OK");
    if (ok) {
        log_info(g_storage_config.logger,
                 "##Query: %u - Bloque Lógico Escrito %s - Número de Bloque: %u",
                 query_id, path_logico, block_idx);
    }

    free(final_block);
    if (estado) free(estado);
    meta_liberar_lista_bloques(bloques);
    return ok;
}






bool fs_commit(uint32_t query_id, const char* path_logico) {
    if (!path_logico_valido(path_logico)) {
        log_error(g_storage_config.logger,
                  "COMMIT(%s): path lógico inválido", path_logico ? path_logico : "(null)");
        return false;
    }

    t_list* bloques = NULL;
    uint32_t tam_logico = 0;
    char* estado = NULL;
    bool ok = meta_cargar(path_logico, &bloques, &tam_logico, &estado);
    if (!ok) {
        log_error(g_storage_config.logger,
                  "COMMIT(%s): metadata inexistente o inválida.", path_logico);
        if (estado) free(estado);
        return false;
    }

    ok = meta_guardar(path_logico, bloques, tam_logico, "COMMITTED");
    if (!ok) {
        log_error(g_storage_config.logger,
                  "COMMIT(%s): meta_guardar falló al poner ESTADO=COMMITTED.", path_logico);
    } else {
        // Log obligatorio: Commit de Tag
        log_info(g_storage_config.logger,
                 "##Query: %u - Commit de File:Tag %s",
                 query_id,
                 path_logico);
    }

    meta_liberar_lista_bloques(bloques);
    if (estado) free(estado);
    return ok;
}

bool fs_delete(uint32_t query_id, const char* path_logico) {
    if (!path_logico_valido(path_logico)) return false;

    t_list* bloques = NULL;
    uint32_t tam = 0;
    char* estado = NULL;

    bool ok = meta_cargar(path_logico, &bloques, &tam, &estado);
    if (!ok) {
        if (estado) free(estado);
        return false;
    }

    char* dir = NULL;
    char* meta_path = NULL;

    if (!_meta_paths(path_logico, &dir, &meta_path)) {
        meta_liberar_lista_bloques(bloques);
        if (estado) free(estado);
        return false;
    }

    struct stat st = {0};
    if (stat(meta_path, &st) != 0) {
        free(dir);
        free(meta_path);
        meta_liberar_lista_bloques(bloques);
        if (estado) free(estado);
        return false;
    }

    if (unlink(meta_path) != 0 && errno != ENOENT) {
        free(dir);
        free(meta_path);
        meta_liberar_lista_bloques(bloques);
        if (estado) free(estado);
        return false;
    }

    // si era el último tag → realmente soltamos referencias de bloques
    if (st.st_nlink == 1) {
        for (int i = 0; i < list_size(bloques); ++i) {
            uint32_t* p = (uint32_t*)list_get(bloques, i);
            if (!p) continue;

            uint32_t b = *p;

            log_info(g_storage_config.logger,
                     "##Query: %u - %s Se eliminó el hard link del bloque lógico %u al bloque físico %u",
                     query_id, path_logico, (uint32_t)i, b);

            if (hash_index_dec_ref_is_zero(b)) {
                fs_liberar_bloque(query_id, b);
                hash_index_remove_block(b);
            }
        }
    }

    if (rmdir(dir) != 0 && errno != ENOTEMPTY && errno != ENOENT) {
        // ok ignorar
    }

    free(dir);
    free(meta_path);
    meta_liberar_lista_bloques(bloques);
    if (estado) free(estado);

    log_info(g_storage_config.logger,
             "##%u - Tag Eliminado %s",
             query_id, path_logico);

    return true;
}



bool fs_tag(uint32_t query_id,
            const char* src_path_logico,
            const char* dst_path_logico) {

    if (!path_logico_valido(src_path_logico) || !path_logico_valido(dst_path_logico))
        return false;

    // 1) Cargar metadata del TAG origen
    t_list* bloques_src = NULL;
    uint32_t tam_src = 0;
    char* estado_src = NULL;

    if (!meta_cargar(src_path_logico, &bloques_src, &tam_src, &estado_src)) {
        log_error(g_storage_config.logger,
                  "TAG: origen '%s' no existe o metadata inválida.",
                  src_path_logico);
        if (estado_src) free(estado_src);
        return false;
    }

    // 2) Armar paths del destino y validar que NO exista
    char* dir_dst = NULL;
    char* meta_dst = NULL;

    if (!_meta_paths(dst_path_logico, &dir_dst, &meta_dst)) {
        meta_liberar_lista_bloques(bloques_src);
        if (estado_src) free(estado_src);
        return false;
    }

    if (access(meta_dst, F_OK) == 0) {
        log_error(g_storage_config.logger,
                  "TAG: destino '%s' ya existe.",
                  dst_path_logico);
        free(dir_dst);
        free(meta_dst);
        meta_liberar_lista_bloques(bloques_src);
        if (estado_src) free(estado_src);
        return false;
    }

    // 3) Crear el directorio del TAG destino
    if (!crear_directorio_rescursivo(dir_dst)) {
        log_error(g_storage_config.logger,
                  "TAG: no pude crear dir destino '%s'",
                  dir_dst);
        free(dir_dst);
        free(meta_dst);
        meta_liberar_lista_bloques(bloques_src);
        if (estado_src) free(estado_src);
        return false;
    }

    // 4) Duplicar los BLOQUES FÍSICOS uno a uno
    t_list* bloques_dst = list_create();
    if (!bloques_dst) {
        log_error(g_storage_config.logger,
                  "TAG: OOM creando lista de bloques destino");
        free(dir_dst);
        free(meta_dst);
        meta_liberar_lista_bloques(bloques_src);
        if (estado_src) free(estado_src);
        return false;
    }

    uint32_t cant_bloques = list_size(bloques_src);

    for (uint32_t i = 0; i < cant_bloques; ++i) {
        uint32_t* p_src = list_get(bloques_src, (int)i);
        if (!p_src) {
            log_error(g_storage_config.logger,
                      "TAG: bloque fuente nulo en índice %u", i);
            for (uint32_t j = 0; j < (uint32_t)list_size(bloques_dst); ++j) {
                uint32_t* p = list_get(bloques_dst, (int)j);
                if (p) {
                    fs_liberar_bloque(query_id, *p);
                }
            }
            meta_liberar_lista_bloques(bloques_dst);
            free(dir_dst);
            free(meta_dst);
            meta_liberar_lista_bloques(bloques_src);
            if (estado_src) free(estado_src);
            return false;
        }

        uint32_t new_block;
        if (!fs_asignar_bloque_libre(query_id, &new_block)) {
            log_error(g_storage_config.logger,
                      "TAG: sin bloques libres al duplicar '%s'",
                      src_path_logico);
            for (uint32_t j = 0; j < (uint32_t)list_size(bloques_dst); ++j) {
                uint32_t* p = list_get(bloques_dst, (int)j);
                if (p) {
                    fs_liberar_bloque(query_id, *p);
                }
            }
            meta_liberar_lista_bloques(bloques_dst);
            free(dir_dst);
            free(meta_dst);
            meta_liberar_lista_bloques(bloques_src);
            if (estado_src) free(estado_src);
            return false;
        }

        // Copiar contenido del bloque físico origen al nuevo bloque físico
        off_t off_src = (off_t)(*p_src) * g_storage_config.block_size;
        off_t off_dst = (off_t)new_block * g_storage_config.block_size;
        void* ptr_src = (char*)g_blocks_dat_map + off_src;
        void* ptr_dst = (char*)g_blocks_dat_map + off_dst;
        memcpy(ptr_dst, ptr_src, g_storage_config.block_size);

        if (!anadir_a_lista(bloques_dst, new_block)) {
            log_error(g_storage_config.logger,
                      "TAG: OOM al agregar bloque destino");

            fs_liberar_bloque(query_id, new_block);
            for (uint32_t j = 0; j < (uint32_t)list_size(bloques_dst); ++j) {
                uint32_t* p = list_get(bloques_dst, (int)j);
                if (p) fs_liberar_bloque(query_id, *p);
            }
            meta_liberar_lista_bloques(bloques_dst);
            free(dir_dst);
            free(meta_dst);
            meta_liberar_lista_bloques(bloques_src);
            if (estado_src) free(estado_src);
            return false;
        }
        // el tag destino referencia este nuevo bloque físico
        hash_index_inc_ref(new_block);


        // Log obligatorio: Hard Link Agregado
        log_info(g_storage_config.logger,
                 "##Query: %u - %s Se agregó el hard link del bloque lógico %u al bloque físico %u",
                 query_id,
                 dst_path_logico,
                 i,
                 new_block);
    }

    bool ok2 = meta_guardar(dst_path_logico,
                            bloques_dst,
                            tam_src,
                            "WORK_IN_PROGRESS");

    if (!ok2) {
        log_error(g_storage_config.logger,
                  "TAG: meta_guardar falló para destino '%s'",
                  dst_path_logico);

        for (uint32_t j = 0; j < (uint32_t)list_size(bloques_dst); ++j) {
            uint32_t* p = list_get(bloques_dst, (int)j);
            if (p) fs_liberar_bloque(query_id, *p);
        }
        meta_liberar_lista_bloques(bloques_dst);
        free(dir_dst);
        free(meta_dst);
        meta_liberar_lista_bloques(bloques_src);
        if (estado_src) free(estado_src);
        return false;
    }

    // Log obligatorio: Creación de Tag
    log_info(g_storage_config.logger,
             "##Query: %u - Tag creado %s",
             query_id,
             dst_path_logico);

    meta_liberar_lista_bloques(bloques_dst);
    meta_liberar_lista_bloques(bloques_src);
    free(dir_dst);
    free(meta_dst);
    if (estado_src) free(estado_src);

    return true;
}
