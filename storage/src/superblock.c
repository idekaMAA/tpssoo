#include "superblock.h"
#include <string.h>
#include <commons/config.h>
#include <stdlib.h> 

bool superbloque_cargar_config(const char* ruta_archivo_superblock, t_superbloque* out)
{
    if (ruta_archivo_superblock == NULL || out == NULL) {
        return false;
    }

    t_config* cfg = config_create((char*) ruta_archivo_superblock);
    if (cfg == NULL) {
        return false;
    }

    char K_FS[] = "FS_SIZE";
    char K_BS[] = "BLOCK_SIZE";

    if (!config_has_property(cfg, K_FS) || !config_has_property(cfg, K_BS)) {
        config_destroy(cfg);
        return false;
    }

    int fs_size   = config_get_int_value(cfg, K_FS);
    int blocksize = config_get_int_value(cfg, K_BS);

    // División por cero o tamaño inválido
    if (blocksize <= 0 || fs_size <= 0) {
        config_destroy(cfg);
        return false;
    }

    out->tam_fs_bytes     = (uint64_t) fs_size;
    out->tam_bloque_bytes = (uint32_t) blocksize;
    out->cantidad_bloques = (uint32_t) (fs_size / blocksize);

    config_destroy(cfg);
    return true;
}