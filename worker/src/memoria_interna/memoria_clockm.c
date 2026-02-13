// ============================================================================
// WORKER - memoria_clockm.c
// PASO A PASO GENERAL
// 1) Mantiene bits de referencia por marco
// 2) Marca y limpia referencias cuando se usan/liberan marcos
// 3) Selecciona marco víctima según CLOCK-M (clases (0,0) y (0,1))
// ============================================================================

#include "memoria_clockm.h"
#include <stdlib.h>

static uint8_t* g_clock_ref   = NULL;
static int      g_clock_hand  = 0;
static uint32_t g_cant_marcos = 0;

// ---------------------------------------------------------------------------
// Init de estructura CLOCK-M
// ---------------------------------------------------------------------------
void memoria_clockm_init(uint32_t cant_marcos) {
    // 1) Liberar vector previo de bits de referencia si existía
    if (g_clock_ref) {
        free(g_clock_ref);
        g_clock_ref = NULL;
    }

    // 2) Guardar cantidad de marcos
    g_cant_marcos = cant_marcos;

    // 3) Reservar vector de bits y posicionar el hand en 0
    if (cant_marcos > 0) {
        g_clock_ref  = calloc(cant_marcos, sizeof(uint8_t));
        g_clock_hand = 0;
    }
}

// ---------------------------------------------------------------------------
// Marcar referencia de marco
// ---------------------------------------------------------------------------
void memoria_clockm_marcar_referencia(int marco) {
    // 1) Validar que exista el vector
    if (!g_clock_ref) return;

    // 2) Validar rango del índice de marco
    if (marco < 0 || (uint32_t)marco >= g_cant_marcos) return;

    // 3) Setear bit de referencia en 1
    g_clock_ref[marco] = 1;
}

// ---------------------------------------------------------------------------
// Limpiar referencia de marco
// ---------------------------------------------------------------------------
void memoria_clockm_limpiar_referencia(int marco) {
    // 1) Validar que exista el vector
    if (!g_clock_ref) return;

    // 2) Validar rango del índice de marco
    if (marco < 0 || (uint32_t)marco >= g_cant_marcos) return;

    // 3) Setear bit de referencia en 0
    g_clock_ref[marco] = 0;
}

// ---------------------------------------------------------------------------
// Seleccionar marco víctima (CLOCK-M mejorado)
// ---------------------------------------------------------------------------
int memoria_clockm_seleccionar_marco(t_list* marcos_fisicos, uint32_t cant_marcos) {
    if (!g_clock_ref || cant_marcos == 0) return -1;

    int candidato_01 = -1;
    int start_hand = g_clock_hand;

    // -------------------------
    // VUELTA 1: buscar (0,0) y registrar (0,1) "real" (ref ya era 0)
    // -------------------------
    for (uint32_t i = 0; i < cant_marcos; i++) {
        int idx = g_clock_hand;

        t_marco* m = list_get(marcos_fisicos, idx);
        t_etp*  e  = (m && m->ocupado) ? m->etp_asociada : NULL;

        if (m && m->ocupado && e) {
            uint8_t r = g_clock_ref[idx];
            uint8_t mod = e->dirty ? 1 : 0;

            if (r == 0 && mod == 0) {
                g_clock_hand = (idx + 1) % (int)cant_marcos;
                return idx; // (0,0) inmediato
            }

            // candidata (0,1) SOLO si ya venía con r==0 (no si la bajamos recién)
            if (r == 0 && mod == 1 && candidato_01 < 0) {
                candidato_01 = idx;
            }

            // segunda oportunidad
            if (r == 1) g_clock_ref[idx] = 0;
        }

        g_clock_hand = (idx + 1) % (int)cant_marcos;
    }

    // Si en la vuelta 1 apareció un (0,1) con ref==0, se elige ese (TP aprobado)
    if (candidato_01 >= 0) {
        g_clock_hand = (candidato_01 + 1) % (int)cant_marcos;
        return candidato_01;
    }

    // -------------------------
    // VUELTA 2: ahora sí buscar (0,0) (ya quedaron refs en 0 por la vuelta 1)
    // -------------------------
    g_clock_hand = start_hand;

    for (uint32_t i = 0; i < cant_marcos; i++) {
        int idx = g_clock_hand;

        t_marco* m = list_get(marcos_fisicos, idx);
        t_etp*  e  = (m && m->ocupado) ? m->etp_asociada : NULL;

        if (m && m->ocupado && e) {
            uint8_t r = g_clock_ref[idx];
            uint8_t mod = e->dirty ? 1 : 0;

            if (r == 0 && mod == 0) {
                g_clock_hand = (idx + 1) % (int)cant_marcos;
                return idx;
            }
        }

        g_clock_hand = (idx + 1) % (int)cant_marcos;
    }

    // Fallback (muy raro): si no encontró nada, devolver el hand actual.
    return start_hand;
}
