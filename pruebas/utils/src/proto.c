#include "proto.h"
#include "net.h"
#include <arpa/inet.h>
#include <stdlib.h>

int enviar_paquete(int fd, uint16_t op_code, const t_paquete* paquete) {
    if (!paquete) return -1;

    t_frame_hdr hdr;
    hdr.opcode = htons(op_code);
    hdr.len    = htonl(paquete->buffer.size);

    if (send_all(fd, &hdr, sizeof(hdr)) != 0) return -1;

    if (paquete->buffer.size > 0 && paquete->buffer.stream) {
        if (send_all(fd, paquete->buffer.stream, paquete->buffer.size) != 0) return -1;
    }
    return 0;
}

int recibir_paquete(int fd, uint16_t* op_code, t_paquete* paquete) {
    if (!op_code || !paquete) return -1;

    t_frame_hdr hdr;
    if (recv_all(fd, &hdr, sizeof(hdr)) != 0) return -1;

    uint16_t op   = ntohs(hdr.opcode);
    uint32_t len  = ntohl(hdr.len);

    paquete->buffer.size   = len;
    paquete->buffer.stream = NULL;

    if (len > 0) {
        paquete->buffer.stream = malloc(len);
        if (!paquete->buffer.stream) return -1;

        if (recv_all(fd, paquete->buffer.stream, len) != 0) {
            free(paquete->buffer.stream);
            paquete->buffer.stream = NULL;
            paquete->buffer.size   = 0;
            return -1;
        }
    }

    *op_code = op;
    return 0;
}
