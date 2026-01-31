#ifndef MENSAJES_QC_H
#define MENSAJES_QC_H
#include <stdint.h>
#define QC_MAX_PATH 4096
typedef struct {
    uint32_t prioridad;
    char     path[QC_MAX_PATH];
} t_submit_query;
#endif
