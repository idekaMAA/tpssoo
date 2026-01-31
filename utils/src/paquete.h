#ifndef PAQUETE_H
#define PAQUETE_H

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t size;
    void* stream;
} t_buffer;

typedef struct {
    t_buffer buffer;
    uint32_t offset; // para leer secuencialmente
} t_paquete;

// Inicialización
void paquete_iniciar(t_paquete* p);

// Cargar datos al paquete
int  paquete_cargar(t_paquete* p, const void* data, uint32_t size);
int  paquete_cargar_cstring(t_paquete* p, const char* s);
int  paquete_cargar_struct(t_paquete* p, const void* s, uint32_t s_size);

// Leer datos del paquete
void* paquete_leer_struct(t_paquete* p, uint32_t size);

// Destruir paquete
void paquete_destruir(t_paquete* p);

// Helpers para uint32_t
int buffer_push_u32(t_paquete* p, uint32_t v_host);
int buffer_pop_u32(const void* src, uint32_t* out_host);


/**
 * @brief Carga un uint32_t (4 bytes) al paquete.
 * @param paquete Puntero al paquete.
 * @param data Valor uint32_t a cargar.
 * @return 0 si éxito, -1 si fallo.
 */
int paquete_cargar_uint32(t_paquete* paquete, uint32_t data);
#endif
int paquete_cargar_datos(t_paquete* paquete, const void* contenido, uint32_t size);