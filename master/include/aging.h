#ifndef AGING_H_
#define AGING_H_

#include "main.h"

uint64_t obtener_timestamp_ms(void);
void* aging_loop(void* arg);

#endif