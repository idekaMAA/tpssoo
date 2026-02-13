#ifndef SUPERBLOCK_H
#define SUPERBLOCK_H

#include <stdbool.h>
#include <stdint.h>

// Estructura para almacenar la metadata del Superbloque
typedef struct {
    uint64_t tam_fs_bytes; 
    uint32_t tam_bloque_bytes; 
    uint32_t cantidad_bloques; 
} t_superbloque;

// Prototipo de función para cargar Superblock (si fuera necesario)
 bool superbloque_cargar_config(const char* ruta_archivo_superblock, t_superbloque* out);

#endif