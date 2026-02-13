#ifndef STORAGE_CONFIG_H
#define STORAGE_CONFIG_H

#include <commons/log.h>
#include <stdbool.h>
#include <stdint.h>

// Definición de la estructura de configuración
typedef struct {
    char* punto_montaje; 
    int   puerto_escucha;  
    bool  fresh_start;     
    uint32_t retardo_operacion; 
    uint32_t retardo_acceso_bloque;
    uint32_t block_size;
    uint32_t fs_size;
    char* log_level;
    t_log* logger; 
} storage_config_t;

// Declaración de la variable global (definida en config.c)
extern storage_config_t g_storage_config; 

// Prototipos de las funciones de configuración
bool leer_configuracion(const char* ruta_config, storage_config_t* cfg);
void storage_config_destruir(storage_config_t* cfg);

// Prototipo de utilidad (usado internamente en config.c)
t_log_level log_level_from_string(char* log_level_str);

#endif /* STORAGE_CONFIG_H */