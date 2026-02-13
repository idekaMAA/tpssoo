#ifndef BLOCKS_HASH_INDEX_H
#define BLOCKS_HASH_INDEX_H

#include <stdint.h>
#include <stdbool.h>

// Inicializa índice. `punto_montaje` puede usarse para persistencia (si luego querés guardar/leer).
bool hash_index_init(const char* punto_montaje, uint32_t cant_bloques);

// Libera estructuras internas (opcional)
void hash_index_destroy(void);

// Guarda índice en disco (si no querés persistencia todavía, puede ser no-op).
void hash_index_save(void);

// Registra/actualiza hash asociado a un bloque físico. (refcount NO se modifica acá)
void hash_index_register(uint32_t block_fisico, const char* md5_hex);

// Remueve por bloque físico (cuando refcount llega a 0).
void hash_index_remove_block(uint32_t block_fisico);

// API vieja que ya tenías usando (mantengo compatibilidad).
// Remueve por bloque físico, devuelve true si existía.
bool hash_index_remove(uint32_t block_fisico);

// Busca un bloque por hash (rápido, sin memcmp). Devuelve true si encuentra alguno.
bool hash_index_find_block(const char* md5_hex, uint32_t* out_block_fisico);

// Busca un bloque con mismo hash y además compara contenido (memcmp contra final_block).
bool hash_index_find_equal_block(const char* md5_hex,
                                 const void* final_block,
                                 uint32_t block_size,
                                 uint32_t* out_block_fisico);

// Refcount: cuando un file/tag empieza a referenciar un bloque
void hash_index_inc_ref(uint32_t block_fisico);

// Refcount: cuando un file/tag deja de referenciar un bloque.
// Devuelve true si el refcount llega a 0.
bool hash_index_dec_ref_is_zero(uint32_t block_fisico);

// Para debug/telemetría (opcional)
uint32_t hash_index_get_ref(uint32_t block_fisico);

#endif
