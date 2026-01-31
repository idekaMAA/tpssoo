#include "paquete.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

void paquete_iniciar(t_paquete* p) {
    p->buffer.size = 0;
    p->buffer.stream = NULL;
    p->offset = 0; // inicializar offset para lectura
}

static int _append_bytes(t_paquete* p, const void* data, uint32_t size) {
    if (size == 0) return 0;
    void* nuevo = realloc(p->buffer.stream, p->buffer.size + size);
    if (!nuevo) return -1;
    memcpy((char*)nuevo + p->buffer.size, data, size);
    p->buffer.stream = nuevo;
    p->buffer.size += size;
    return 0;
}

int paquete_cargar(t_paquete* p, const void* data, uint32_t size) {
    return _append_bytes(p, data, size);
}

int paquete_cargar_cstring(t_paquete* p, const char* s) {
    size_t n = strlen(s) + 1;
    if (n > 0xFFFFFFFFu) return -1;
    return _append_bytes(p, s, (uint32_t)n);
}

int paquete_cargar_struct(t_paquete* p, const void* s, uint32_t s_size) {
    return _append_bytes(p, s, s_size);
}

void* paquete_leer_struct(t_paquete* p, uint32_t size) {
    if (!p || size == 0) return NULL;
    if (p->offset + size > p->buffer.size) return NULL; // evitar overflow

    void* datos = malloc(size);
    memcpy(datos, (char*)p->buffer.stream + p->offset, size);
    p->offset += size;
    return datos;
}

void paquete_destruir(t_paquete* p) {
    if (!p) return;
    if (p->buffer.stream) free(p->buffer.stream);
    p->buffer.stream = NULL;
    p->buffer.size = 0;
    p->offset = 0;
}

int buffer_push_u32(t_paquete* p, uint32_t v_host) {
    uint32_t be = htonl(v_host);
    return _append_bytes(p, &be, sizeof(be));
}

int buffer_pop_u32(const void* src, uint32_t* out_host) {
    if (!src || !out_host) return -1;
    uint32_t be;
    memcpy(&be, src, sizeof(be));
    *out_host = ntohl(be);
    return 0;
}

int paquete_cargar_uint32(t_paquete* paquete, uint32_t data) {
    if (!paquete) return -1;
    paquete->buffer.stream = realloc(paquete->buffer.stream, paquete->buffer.size + sizeof(uint32_t));
    if (!paquete->buffer.stream) return -1;
    memcpy(paquete->buffer.stream + paquete->buffer.size, &data, sizeof(uint32_t));
    paquete->buffer.size += sizeof(uint32_t);
    return 0;
}

int paquete_cargar_datos(t_paquete* paquete, const void* contenido, uint32_t size) {
    if (!paquete || !contenido || size == 0) return -1;
    
    // 1. Reasignar memoria para el nuevo contenido
    paquete->buffer.stream = realloc(paquete->buffer.stream, paquete->buffer.size + size);
    
    // 2. Copiar los nuevos datos al final del buffer
    memcpy(paquete->buffer.stream + paquete->buffer.size, contenido, size);
    
    // 3. Actualizar el tamaño total del paquete
    paquete->buffer.size += size;

    return 0; // Éxito
}
