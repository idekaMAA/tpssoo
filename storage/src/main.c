#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>     
#include <commons/log.h>
#include "config.h"
#include "inicializacion.h"
#include "servidor_storage.h" 
#include "storage_utils.h" 
#include "blocks_hash_index.h"


int main(int argc, char *argv[]){
    
    if (argc < 2) {
        fprintf(stderr, "Error: Debe proporcionar la ruta al archivo de configuración.\n");
        fprintf(stderr, "Uso: %s <ruta/a/storage.cfg>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    const char* ruta_config = argv[1]; 

    // Inicialización de Configuración y Logger
    if(!leer_configuracion(ruta_config, &g_storage_config)){ 
        fprintf(stderr, "Fallo critico al cargar la configuracion.\n");
        return EXIT_FAILURE;
    }
    
    log_info(g_storage_config.logger, "Configuración cargada exitosamente de %s", ruta_config);

    // Inicialización de Sincronización Multihilo
    init_multihilo(g_storage_config.logger); 
    
    // Inicialización del File System
    // ... (Lógica de FRESH_START o FS existente) ...
   if (g_storage_config.fresh_start) {
    log_warning(g_storage_config.logger, "Inicializando FS bajo FRESH_START...");

    if (!init_nuevo_fs(g_storage_config.punto_montaje)) {
        log_error(g_storage_config.logger, "Fallo critico al iniciar FS nuevo.");
        storage_config_destruir(&g_storage_config);
        return EXIT_FAILURE;
    }

} else {
    log_info(g_storage_config.logger, "Cargando FS existente...");

    if (!fs_actual(g_storage_config.punto_montaje)) {
        log_error(g_storage_config.logger, "Fallo critico al cargar FS actual.");
        storage_config_destruir(&g_storage_config);
        return EXIT_FAILURE;
    }
}

if (!crear_init_arch(g_storage_config.punto_montaje)) {
    log_error(g_storage_config.logger, "Fallo al crear archivo inicial.");
    storage_config_destruir(&g_storage_config);
    return EXIT_FAILURE;
}


    
    // Iniciar Servidor
    
    char puerto_str[6]; 
    snprintf(puerto_str, sizeof(puerto_str), "%d", g_storage_config.puerto_escucha);

    log_info(g_storage_config.logger, "Servidor Storage iniciando y escuchando en puerto %d.", g_storage_config.puerto_escucha);
    
    if (iniciar_servidor_storage(puerto_str) != 0) {
        log_error(g_storage_config.logger, "Fallo al inicializar servidor de Workers. El programa terminará.");
        storage_config_destruir(&g_storage_config); 
        return EXIT_FAILURE;
    }

    // Bloquear el hilo principal 
    esperar_worker(g_storage_config.logger); 

    // Limpieza 
    hash_index_save();
    hash_index_destroy();
    log_info(g_storage_config.logger, "Apagado y limpieza de recursos.");
    utils_destroy_sync(g_storage_config.logger);
    storage_config_destruir(&g_storage_config); 
    
    return EXIT_SUCCESS;
}
