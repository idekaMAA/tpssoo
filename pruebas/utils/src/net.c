#define _POSIX_C_SOURCE 200112L
#include "net.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int conectar_a(const char* ip, const char* puerto) {
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(ip, puerto, &hints, &res) != 0) return -1;
    int fd = -1;
    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int escuchar_en(const char* puerto) {
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, puerto, &hints, &res) != 0) return -1;
    int fd = -1;
    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) continue;
        if (bind(fd, p->ai_addr, p->ai_addrlen) == 0 && listen(fd, 64) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int send_all(int fd, const void* buf, uint32_t len) {
    const char* p = (const char*)buf;
    uint32_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += (uint32_t)n;
    }
    return 0;
}

int recv_all(int fd, void* buf, uint32_t len) {
    char* p = (char*)buf;
    uint32_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(fd, p + recvd, len - recvd, 0);
        if (n <= 0) return -1;
        recvd += (uint32_t)n;
    }
    return 0;
}
