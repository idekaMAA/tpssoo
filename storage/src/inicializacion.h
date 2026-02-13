#ifndef INICIALIZACION_H
#define INICIALIZACION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h" 
#include "superblock.h" 
#include "fs_manager.h"
#include "storage_utils.h"
#include "blocks_hash_index.h"
#include <commons/bitarray.h>


// --- Variables Globales del FS ---
extern void* g_blocks_dat_map;
extern int g_blocks_dat_fd;
extern void*       g_bitmap_map;
extern size_t      g_bitmap_size;
extern t_bitarray* g_bitmap;

// Funciones de utilidad de directorios
bool crear_directorio(const char* base, const char* sub_path);
bool crear_directorio_rescursivo(const char* ruta_completa);
bool eliminar_contenido(const char* ruta);

// Funciones de utilidad de archivos
bool crear_archivo(const char* ruta_completa);
bool crear_relleno_tam_fijo(const char* ruta_completa, size_t tam, char fill);
bool hard_link(const char* origen, const char* destino);

// Funciones principales de inicialización
bool init_nuevo_fs(const char* punto_montaje);
bool fs_actual(const char* punto_montaje);
bool crear_init_arch(const char* punto_montaje);

// Funciones auxiliares del FS y Memory Mapping (Mmap)
bool crear_superbloque(const char* punto_montaje);
bool crear_bitmap(const char* punto_montaje);
bool crear_bloques_fisicos(const char* punto_montaje);
bool fs_mapear_data_blocks(const char* punto_montaje);
void fs_desmapear_data_blocks(void);

// Funciones funcionamiento de bitmap
bool fs_mapear_bitmap(const char* punto_montaje);
void fs_desmapear_bitmap(void);

#endif /* INICIALIZACION_H */
