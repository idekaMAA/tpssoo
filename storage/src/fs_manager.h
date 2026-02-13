#ifndef FS_MANAGER_H
#define FS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <commons/collections/list.h>

// ==========================
//  BLOQUES FÍSICOS
// ==========================

bool fs_asignar_bloque_libre(uint32_t query_id,
                             uint32_t* bloque_fisico_idx);

bool fs_liberar_bloque(uint32_t query_id,
                       uint32_t bloque_fisico_idx);

// ==========================
//  CREAR FILE:TAG
// ==========================

bool fs_crear_file_tag(uint32_t query_id,
                       const char* punto_montaje,
                       const char* file_tag_name);

// ==========================
//  LEER BLOQUE (API vieja - se conserva, NO se elimina)
// ==========================

ssize_t fs_leer_bloque(uint32_t query_id,
                       const char* punto_montaje,
                       const char* file_tag_name,
                       uint32_t bloque_logico_idx,
                       void* buffer_salida,
                       uint32_t tam_a_leer);

// ==========================
//  LEER BLOQUE COMPLETO POR ÍNDICE LÓGICO (API nueva con query_id)
//  - Necesaria para trazabilidad y consistencia con "lo de los querys"
// ==========================

bool fs_read_bloque(uint32_t query_id,
                    const char* path_logico,
                    uint32_t block_idx,
                    void* out_block,
                    uint32_t block_size);

// ==========================
//  TRUNCAR FILE:TAG
// ==========================

bool fs_truncate(uint32_t query_id,
                 const char* path_logico,
                 uint32_t new_size);

// ==========================
//  ESCRIBIR BLOQUE
// ==========================

bool fs_write_bloque(uint32_t query_id,
                     const char* path_logico,
                     uint32_t block_idx,
                     const void* data,
                     uint32_t len);

// ==========================
//  COMMIT
// ==========================

bool fs_commit(uint32_t query_id,
               const char* path_logico);

// ==========================
//  DELETE FILE:TAG
// ==========================

bool fs_delete(uint32_t query_id,
               const char* path_logico);

// ==========================
//  TAG (COPIA DE FILE:TAG)
// ==========================

bool fs_tag(uint32_t query_id,
            const char* src_path_logico,
            const char* dst_path_logico);

#endif // FS_MANAGER_H
