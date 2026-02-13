// ============================================================================
// WORKER - memoria_lru.h
// PASO A PASO GENERAL
// 1) Expone selección de marco víctima usando LRU
// 2) Se usa desde memoria_interna para reemplazo de páginas
// ============================================================================

#ifndef MEMORIA_LRU_H
#define MEMORIA_LRU_H

#include <stdint.h>
#include <commons/collections/list.h>
#include "memoria_interna.h"

// ---------------------------------------------------------------------------
// API LRU
// ---------------------------------------------------------------------------
int memoria_lru_seleccionar_marco(t_list* marcos_fisicos, uint32_t cant_marcos);

#endif // MEMORIA_LRU_H
