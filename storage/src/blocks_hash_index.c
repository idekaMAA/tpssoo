#include "blocks_hash_index.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "inicializacion.h"   // por extern de g_storage_config / g_blocks_dat_map si ahí están
// Si esos extern están en otro header, incluí el correcto.
extern storage_config_t g_storage_config;
extern void* g_blocks_dat_map;

typedef struct {
    bool     used;
    char     md5[33];     // 32 hex + '\0'
    uint32_t refcount;
} t_block_entry;

static t_block_entry* g_entries = NULL;
static uint32_t       g_cap     = 0;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

// --- helpers ---
static inline size_t _block_offset_u32(uint32_t block_fisico) {
    return (size_t)block_fisico * (size_t)g_storage_config.block_size;
}

bool hash_index_init(const char* punto_montaje, uint32_t cant_bloques) {
    (void)punto_montaje;

    pthread_mutex_lock(&g_mtx);

    if (g_entries) {
        pthread_mutex_unlock(&g_mtx);
        return true;
    }

    g_entries = (t_block_entry*)calloc((size_t)cant_bloques, sizeof(t_block_entry));
    if (!g_entries) {
        pthread_mutex_unlock(&g_mtx);
        return false;
    }

    g_cap = cant_bloques;

    pthread_mutex_unlock(&g_mtx);
    return true;
}

void hash_index_destroy(void) {
    pthread_mutex_lock(&g_mtx);
    free(g_entries);
    g_entries = NULL;
    g_cap = 0;
    pthread_mutex_unlock(&g_mtx);
}

void hash_index_save(void) {
    // Por ahora no-op: lo importante es que exista el símbolo para linkear main.c.
    // Si después querés persistirlo: guardamos md5/refcount por block en un archivo.
}

void hash_index_register(uint32_t block_fisico, const char* md5_hex) {
    if (!md5_hex) return;

    pthread_mutex_lock(&g_mtx);

    if (!g_entries || block_fisico >= g_cap) {
        pthread_mutex_unlock(&g_mtx);
        return;
    }

    t_block_entry* e = &g_entries[block_fisico];
    e->used = true;
    strncpy(e->md5, md5_hex, 32);
    e->md5[32] = '\0';

    // OJO: NO tocamos refcount acá. Refcount se maneja con inc/dec
    pthread_mutex_unlock(&g_mtx);
}

bool hash_index_remove(uint32_t block_fisico) {
    pthread_mutex_lock(&g_mtx);

    if (!g_entries || block_fisico >= g_cap) {
        pthread_mutex_unlock(&g_mtx);
        return false;
    }

    bool existed = g_entries[block_fisico].used;
    g_entries[block_fisico].used = false;
    g_entries[block_fisico].md5[0] = '\0';
    g_entries[block_fisico].refcount = 0;

    pthread_mutex_unlock(&g_mtx);
    return existed;
}

void hash_index_remove_block(uint32_t block_fisico) {
    (void)hash_index_remove(block_fisico);
}

bool hash_index_find_block(const char* md5_hex, uint32_t* out_block_fisico) {
    if (!md5_hex || !out_block_fisico) return false;

    pthread_mutex_lock(&g_mtx);

    if (!g_entries) {
        pthread_mutex_unlock(&g_mtx);
        return false;
    }

    for (uint32_t i = 0; i < g_cap; ++i) {
        if (g_entries[i].used && strncmp(g_entries[i].md5, md5_hex, 33) == 0) {
            *out_block_fisico = i;
            pthread_mutex_unlock(&g_mtx);
            return true;
        }
    }

    pthread_mutex_unlock(&g_mtx);
    return false;
}

bool hash_index_find_equal_block(const char* md5_hex,
                                 const void* final_block,
                                 uint32_t block_size,
                                 uint32_t* out_block_fisico)
{
    if (!md5_hex || !final_block || !out_block_fisico) return false;
    if (!g_blocks_dat_map) return false;

    pthread_mutex_lock(&g_mtx);

    if (!g_entries) {
        pthread_mutex_unlock(&g_mtx);
        return false;
    }

    for (uint32_t i = 0; i < g_cap; ++i) {
        if (!g_entries[i].used) continue;
        if (strncmp(g_entries[i].md5, md5_hex, 33) != 0) continue;

        // Confirmación por contenido
        void* ptr = (char*)g_blocks_dat_map + _block_offset_u32(i);
        if (memcmp(ptr, final_block, (size_t)block_size) == 0) {
            *out_block_fisico = i;
            pthread_mutex_unlock(&g_mtx);
            return true;
        }
    }

    pthread_mutex_unlock(&g_mtx);
    return false;
}

void hash_index_inc_ref(uint32_t block_fisico) {
    pthread_mutex_lock(&g_mtx);

    if (!g_entries || block_fisico >= g_cap) {
        pthread_mutex_unlock(&g_mtx);
        return;
    }

    g_entries[block_fisico].refcount++;

    pthread_mutex_unlock(&g_mtx);
}

bool hash_index_dec_ref_is_zero(uint32_t block_fisico) {
    pthread_mutex_lock(&g_mtx);

    if (!g_entries || block_fisico >= g_cap) {
        pthread_mutex_unlock(&g_mtx);
        return false;
    }

    if (g_entries[block_fisico].refcount == 0) {
        pthread_mutex_unlock(&g_mtx);
        return true;
    }

    g_entries[block_fisico].refcount--;

    bool is_zero = (g_entries[block_fisico].refcount == 0);

    pthread_mutex_unlock(&g_mtx);
    return is_zero;
}

uint32_t hash_index_get_ref(uint32_t block_fisico) {
    pthread_mutex_lock(&g_mtx);

    uint32_t r = 0;
    if (g_entries && block_fisico < g_cap) {
        r = g_entries[block_fisico].refcount;
    }

    pthread_mutex_unlock(&g_mtx);
    return r;
}
