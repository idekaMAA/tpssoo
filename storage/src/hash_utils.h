#ifndef HASH_UTILS_H
#define HASH_UTILS_H

#include <stddef.h>   // para size_t

// Calcula el MD5 de un buffer y lo devuelve como string hex de 32 chars + '\0'
void calcular_md5(const void* data, size_t len, char out_hex[33]);

#endif
