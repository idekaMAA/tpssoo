#include "hash_utils.h"
#include <commons/crypto.h>   // crypto_md5
#include <stdlib.h>
#include <string.h>

void calcular_md5(const void* data, size_t len, char out_hex[33]) {
    // crypto_md5 devuelve un char* heap-allocado con 32 hex + '\0'
    char* tmp = crypto_md5((void*)data, len);
    if (!tmp) {
        // por las dudas, dejá algo consistente
        memset(out_hex, '0', 32);
        out_hex[32] = '\0';
        return;
    }

    memcpy(out_hex, tmp, 32);
    out_hex[32] = '\0';
    free(tmp);
}
