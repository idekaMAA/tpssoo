#include "memoria_lru.h"

int memoria_lru_seleccionar_marco(t_list* marcos_fisicos, uint32_t cant_marcos) {
    int elegido = -1;
    unsigned long tick_min = 0;

    for (int i = 0; i < (int)cant_marcos; i++) {
        t_marco* m = list_get(marcos_fisicos, i);
        if (!m->ocupado || !m->etp_asociada) continue;

        if (elegido < 0 || m->etp_asociada->ultimo_uso < tick_min) {
            elegido  = i;
            tick_min = m->etp_asociada->ultimo_uso;
        }
    }

    return elegido;
}
