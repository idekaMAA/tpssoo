#ifndef STORAGE_UTILS_H
#define STORAGE_UTILS_H

#include <commons/log.h>
#include <stdint.h>
#include <pthread.h>

extern pthread_mutex_t g_bitmap_mtx; 

// Inicializa los recursos de sincronización multihilo (mutex, semáforos, etc.)

void init_multihilo(t_log* logger);

// Función de barrera para mantener el servidor activo hasta recibir una señal de terminación.
void esperar_worker(t_log* logger);
void bitmap_liberar_bloque(uint32_t bloque);
int32_t bitmap_reservar_nuevo_bloque(void);

// Destruye mutex
void utils_destroy_sync(t_log* logger);

#endif // STORAGE_UTILS_H

