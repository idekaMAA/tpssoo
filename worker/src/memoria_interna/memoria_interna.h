// ============================================================================
// WORKER - memoria_interna.h
// PASO A PASO GENERAL
// 1) Define estructuras de tabla de páginas, marcos y páginas (ETP)
// 2) Expone la API de memoria interna para el Worker
// ============================================================================

#ifndef MEMORIA_INTERNA_H
#define MEMORIA_INTERNA_H

#include <stdint.h>
#include <commons/log.h>
#include <commons/collections/list.h>
#include "../query_interpreter/instrucciones_parser.h"

// ---------------------------------------------------------------------------
// Estructuras públicas
// ---------------------------------------------------------------------------
typedef struct {
    file_tag_t      ft;
    uint32_t        nro_pagina;
    uint32_t        nro_marco;
    uint32_t        id_bloque_storage;
    uint8_t         presencia;
    uint8_t         dirty;
    uint32_t        query_id;
    unsigned long   ultimo_uso;
} t_etp;

typedef struct {
    file_tag_t ft;
    t_list*    entradas;
} t_tabla_paginas;

typedef struct {
    uint32_t nro_marco;
    uint8_t  ocupado;
    t_etp*   etp_asociada;
} t_marco;

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------
int      memoria_init(uint32_t tam_memoria,uint32_t block_size,const char* algoritmo,t_log* logger);

uint32_t memoria_get_block_size(void);

int      memoria_escribir(file_tag_t ft,uint32_t dir_base,uint32_t size,const char* data,int query_id,int fd_storage,t_log* logger,uint32_t retardo_ms);

int      memoria_leer(file_tag_t ft,uint32_t dir_base,uint32_t size,char* destino,int query_id,int fd_storage,t_log* logger,uint32_t retardo_ms);

int      memoria_flush(file_tag_t ft,int fd_storage,t_log* logger);

void     memoria_destroy(void);

void     memoria_flush_global(void);
void     memoria_flush_implicito(uint32_t query_id);

// liberar recursos de la Query sin persistir (END normal)
void     memoria_liberar_implicito(uint32_t query_id);

void     memoria_registrar_pc(uint32_t query_id, uint32_t pc);
uint32_t query_pc_actual(uint32_t query_id);

#endif // MEMORIA_INTERNA_H
