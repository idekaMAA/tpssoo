#ifndef NET_H
#define NET_H
#include <stdint.h>
int conectar_a(const char* ip, const char* puerto);
int escuchar_en(const char* puerto);
int send_all(int fd, const void* buf, uint32_t len);
int recv_all(int fd, void* buf, uint32_t len);
#endif
