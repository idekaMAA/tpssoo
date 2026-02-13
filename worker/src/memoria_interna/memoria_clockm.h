// ============================================================================
// WORKER - memoria_clockm.h
// PASO A PASO GENERAL
// 1) Expone la API de CLOCK-M para memoria_interna
// 2) Inicializa estructura de referencia
// 3) Marca/limpia referencias y elige marcos víctima
// ============================================================================

#ifndef MEMORIA_CLOCKM_H
#define MEMORIA_CLOCKM_H

#include <stdint.h>
#include <commons/collections/list.h>
#include "memoria_interna.h"

// ---------------------------------------------------------------------------
// API CLOCK-M
// ---------------------------------------------------------------------------
void memoria_clockm_init(uint32_t cant_marcos);
void memoria_clockm_marcar_referencia(int marco);
void memoria_clockm_limpiar_referencia(int marco);
int  memoria_clockm_seleccionar_marco(t_list* marcos_fisicos, uint32_t cant_marcos);

#endif // MEMORIA_CLOCKM_H
