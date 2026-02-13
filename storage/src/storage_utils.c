#include "storage_utils.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <commons/bitarray.h>
#include "inicializacion.h"
#include <sys/mman.h>
#include "config.h"

#include <pthread.h> 

pthread_mutex_t g_bitmap_mtx; 
extern void* g_bitmap_map;
extern size_t g_bitmap_size;
extern t_bitarray* g_bitmap;  
extern storage_config_t g_storage_config;

// Implementación de init_multihilo
void init_multihilo(t_log* logger) {
    log_info(logger, "Inicializando recursos de sincronización multihilo (mutex, semaforos, etc.).");
    if (pthread_mutex_init(&g_bitmap_mtx, NULL) != 0) {
        log_error(logger, "Error al inicializar g_bitmap_mtx");
    }
}

void esperar_worker(t_log* logger) {

    log_info(logger, "Servidor Storage activo. Esperando señal de terminación (Ctrl+C).");
    
    sigset_t set;
  
    if (sigemptyset(&set) != 0 || sigaddset(&set, SIGINT) != 0) {
        log_error(logger, "Error al configurar el conjunto de señales: %s", strerror(errno));
        return;
    }
    

    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        log_error(logger, "Error al bloquear SIGINT: %s", strerror(errno));
        return;
    }

    int sig;

    if (sigwait(&set, &sig) == 0) {
        log_warning(logger, "Señal de terminación (%d) recibida. Cerrando servidor Storage.", sig);
    } else {
        log_error(logger, "Error al esperar la señal de terminación: %s", strerror(errno));
    }

}
void utils_destroy_sync(t_log* logger) {
    pthread_mutex_destroy(&g_bitmap_mtx);
    log_info(logger, "Sincronización destruida.");
} 

int32_t bitmap_reservar_nuevo_bloque(void) {
    if (!g_bitmap) return -1;

    pthread_mutex_lock(&g_bitmap_mtx);

    uint32_t num_bloques = g_storage_config.fs_size / g_storage_config.block_size;
    int32_t elegido = -1;

    for (uint32_t i = 0; i < num_bloques; ++i) {
        if (!bitarray_test_bit(g_bitmap, i)) {
            bitarray_set_bit(g_bitmap, i);
            elegido = (int32_t)i;
            msync(g_bitmap_map, g_bitmap_size, MS_SYNC);
            break;
        }
    }

    pthread_mutex_unlock(&g_bitmap_mtx);
    return elegido;
}

void bitmap_liberar_bloque(uint32_t bloque) {
    if (!g_bitmap) return;

    pthread_mutex_lock(&g_bitmap_mtx);

    uint32_t num_bloques = g_storage_config.fs_size / g_storage_config.block_size;
    if (bloque < num_bloques) {
        bitarray_clean_bit(g_bitmap, bloque);
        msync(g_bitmap_map, g_bitmap_size, MS_SYNC);
    } else {
        log_error(g_storage_config.logger, "bitmap_liberar_bloque: índice fuera de rango (%u)", bloque);
    }

    pthread_mutex_unlock(&g_bitmap_mtx);
}

