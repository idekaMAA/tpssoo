// ============================================================================
// WORKER - instrucciones_parser.h
// PASO A PASO GENERAL
// 1) Define file_tag_t para parámetros "file:tag"
// 2) Define instruccion_t con opcode, params y files
// 3) Expone funciones de parseo y destrucción de instrucciones
// ============================================================================

#ifndef INSTRUCTION_PARSER_H
#define INSTRUCTION_PARSER_H

typedef struct {
    char* file;
    char* tag;
} file_tag_t;

typedef struct {
    char*      opcode;
    char**     params;
    int        param_count;
    file_tag_t* files;
    int        file_count;
} instruccion_t;

instruccion_t* parsear_linea(const char* linea);
void           destruir_instruccion(instruccion_t* inst);

#endif
