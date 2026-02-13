#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <commons/config.h>  
#include <commons/string.h>  
#include "config.h"
#include <commons/log.h>
#include <limits.h>

storage_config_t g_storage_config;

t_log_level log_level_from_string(char* log_level_str) { 
    if (string_equals_ignore_case(log_level_str, (char*)"TRACE")) return LOG_LEVEL_TRACE;
    if (string_equals_ignore_case(log_level_str, (char*)"DEBUG")) return LOG_LEVEL_DEBUG;
    if (string_equals_ignore_case(log_level_str, (char*)"INFO")) return LOG_LEVEL_INFO;
    if (string_equals_ignore_case(log_level_str, (char*)"WARNING")) return LOG_LEVEL_WARNING;
    if (string_equals_ignore_case(log_level_str, (char*)"ERROR")) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

static void cfg_limpiar_campos_parciales(storage_config_t* cfg) {
    if (cfg->punto_montaje) free(cfg->punto_montaje);
    if (cfg->log_level) free(cfg->log_level);
    memset(cfg, 0, sizeof(storage_config_t));
}

void storage_config_destruir(storage_config_t* cfg) {
    if (!cfg) return;
    if (cfg->punto_montaje) free(cfg->punto_montaje);
    if (cfg->log_level) free(cfg->log_level);
    if (cfg->logger) log_destroy(cfg->logger);
    
    memset(cfg, 0, sizeof(storage_config_t));
}

// Función principal de carga de configuración
bool leer_configuracion(const char* ruta_config, storage_config_t* cfg) {

    t_config* archivo = config_create((char*) ruta_config);
    
    if (archivo == NULL) {
        fprintf(stderr, "No se pudo abrir el archivo de configuracion en %s\n", ruta_config);
        return false; 
    }
    
    #define CLAVE_OBLIGATORIA(KEY) \
        if (!config_has_property(archivo, KEY)) { \
            fprintf(stderr, "Falta clave requerida: %s\n", KEY); \
            config_destroy(archivo); \
            cfg_limpiar_campos_parciales(cfg); \
            return false; \
        }

    memset(cfg, 0, sizeof(storage_config_t));

    // Carga de propiedades
    CLAVE_OBLIGATORIA("PUNTO_MONTAJE");
    cfg->punto_montaje = strdup(config_get_string_value(archivo, "PUNTO_MONTAJE"));

    CLAVE_OBLIGATORIA("PUERTO_ESCUCHA");
    cfg->puerto_escucha = config_get_int_value(archivo, "PUERTO_ESCUCHA");

    CLAVE_OBLIGATORIA("FRESH_START");
    char* fs_str = config_get_string_value(archivo, "FRESH_START");
    cfg->fresh_start = string_equals_ignore_case(fs_str, (char*)"TRUE");

    CLAVE_OBLIGATORIA("RETARDO_OPERACION");
    cfg->retardo_operacion = (uint32_t)config_get_int_value(archivo, "RETARDO_OPERACION");

    CLAVE_OBLIGATORIA("RETARDO_ACCESO_BLOQUE");
    cfg->retardo_acceso_bloque = (uint32_t)config_get_int_value(archivo, "RETARDO_ACCESO_BLOQUE");
    
    CLAVE_OBLIGATORIA("BLOCK_SIZE");
    cfg->block_size = (uint32_t)config_get_int_value(archivo, "BLOCK_SIZE");

    // FS_SIZE es opcional o se carga de un valor por defecto
    if (config_has_property(archivo, "FS_SIZE")) {
        cfg->fs_size = (uint32_t)config_get_int_value(archivo, "FS_SIZE");
    } else {
        cfg->fs_size = 4096; 
    }

    if (cfg->block_size > 0 && (cfg->block_size & (cfg->block_size - 1)) != 0) {
        log_error(cfg->logger, "BLOCK_SIZE (%u) debe ser potencia de 2.", cfg->block_size);
        storage_config_destruir(cfg);
        return false;
    }

    CLAVE_OBLIGATORIA("LOG_LEVEL");
    cfg->log_level = strdup(config_get_string_value(archivo, "LOG_LEVEL"));

    config_destroy(archivo);
    
    const char* log_file_path = "storage.log"; 

    cfg->logger = log_create(
        (char*) log_file_path, 
        "STORAGE",             
        true,                  
        log_level_from_string(cfg->log_level)
    );

    if (cfg->logger == NULL) {
        fprintf(stderr, "Error critico al crear el logger.\n");
        storage_config_destruir(cfg); 
        return false;
    }

    if (cfg->block_size == 0) {
        log_error(cfg->logger, "Tamaño de bloque no puede ser cero.");
        storage_config_destruir(cfg);
        return false;
    }
    
    return true; 
}