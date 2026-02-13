// ============================================================================
// WORKER - instrucciones_parser.c
// PASO A PASO GENERAL
// 1) Parsear tokens "file:tag" a estructuras file_tag_t
// 2) Parsear líneas de Query en instruccion_t (opcode + params + files)
// 3) Liberar correctamente una instruccion_t
// ============================================================================

#include "instrucciones_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Parseo de token "file:tag" a file_tag_t
// ---------------------------------------------------------------------------
static file_tag_t parse_file_tag(const char* token) {
    // 1) Inicializar estructura vacía
    file_tag_t ft = {NULL, NULL};

    // 2) Duplicar el token para poder modificarlo
    char* copy = strdup(token);

    // 3) Buscar separador ':' entre file y tag
    char* sep = strchr(copy, ':');
    if (sep) {
        // 3.a) Cortar en ':' y separar file / tag
        *sep = '\0';
        ft.file = strdup(copy);
        ft.tag  = strdup(sep + 1);
    } else {
        // 3.b) Si no hay ':', solo se guarda el file
        ft.file = strdup(copy);
        ft.tag  = NULL;
    }

    // 4) Liberar copia temporal y devolver estructura
    free(copy);
    return ft;
}

// ---------------------------------------------------------------------------
// Parseo de una línea completa de Query a instruccion_t
// ---------------------------------------------------------------------------
instruccion_t* parsear_linea(const char* linea) {
    // 1) Validar que la línea no sea nula o vacía
    if (!linea || !*linea)
        return NULL;

    // 2) Duplicar la línea para tokenizar sin modificar el original
    char* copy = strdup(linea);
    if (!copy)
        return NULL;

    // 3) Reservar estructura instruccion_t inicializada en cero
    instruccion_t* inst = calloc(1, sizeof(instruccion_t));

    // 4) Tomar la primera palabra como opcode
    char* token = strtok(copy, " \t\n");
    if (!token) {
        free(copy);
        free(inst);
        return NULL;
    }
    inst->opcode = strdup(token);

    // 5) Recorrer el resto de los tokens
    //    Regla: primero vienen los File:Tag (tokens con ':'),
    //    y desde que aparece el primer token SIN ':' todo lo que sigue
    //    se trata como parámetro, aunque tenga ':'.
    int en_zona_params = 0;

    while ((token = strtok(NULL, " \t\n")) != NULL) {
        if (!en_zona_params && strchr(token, ':')) {
            // Todavía estamos en la zona de File:Tag
            inst->files = realloc(inst->files, sizeof(file_tag_t) * (inst->file_count + 1));
            inst->files[inst->file_count] = parse_file_tag(token);
            inst->file_count++;
        } else {
            // A partir de aquí, todo son parámetros
            en_zona_params = 1;
            inst->params = realloc(inst->params, sizeof(char*) * (inst->param_count + 1));
            inst->params[inst->param_count] = strdup(token);
            inst->param_count++;
        }
    }

    // 6) Liberar copia temporal y devolver la instrucción parseada
    free(copy);
    return inst;
}

// ---------------------------------------------------------------------------
// Liberar una instruccion_t completa
// ---------------------------------------------------------------------------
void destruir_instruccion(instruccion_t* inst) {
    // 1) Validar puntero
    if (!inst) return;

    // 2) Liberar opcode
    free(inst->opcode);

    // 3) Liberar parámetros comunes
    for (int i = 0; i < inst->param_count; i++) {
        free(inst->params[i]);
    }
    free(inst->params);

    // 4) Liberar parámetros File:Tag
    for (int i = 0; i < inst->file_count; i++) {
        free(inst->files[i].file);
        free(inst->files[i].tag);
    }
    free(inst->files);

    // 5) Liberar estructura principal
    free(inst);
}
