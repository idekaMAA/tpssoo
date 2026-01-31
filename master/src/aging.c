#include "../include/aging.h"
#include "../include/inicializaciones.h"
#include "../include/auxiliares.h"

#include <sys/time.h>

uint64_t obtener_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000ULL) + (tv.tv_usec / 1000ULL);
}

void* aging_loop(void* arg) {
    while (1) {
        if (config_master.tiempo_aging == 0 || strcmp(config_master.algoritmo_planificacion, "PRIORIDADES") != 0) {
            usleep(1000 * 1000);
            continue;
        }
        
        usleep(config_master.tiempo_aging * 1000);
        
        pthread_mutex_lock(&mutex_ready);
        
        uint64_t ahora = obtener_timestamp_ms();
        int cambios = 0;

        for (int i = 0; i < list_size(cola_ready); i++) {
            t_query* q = list_get(cola_ready, i);
            uint64_t diff = ahora - q->tiempo_entrada_ready;

            if (diff >= config_master.tiempo_aging && q->prioridad > 0) {
                q->prioridad -= 1;
                q->tiempo_entrada_ready = ahora;
                cambios++;
                log_info(logger, "## %d Cambio de prioridad %d - %d", q->id, q->prioridad + 1, q->prioridad); // OBLIGATORIO
            }
        }

        if (cambios > 0 && strcmp(config_master.algoritmo_planificacion, "PRIORIDADES") == 0) {
            list_sort(cola_ready, comparar_prioridad);
        }

        pthread_mutex_unlock(&mutex_ready);
    }

    return NULL;
}
