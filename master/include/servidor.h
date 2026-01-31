#ifndef SERVIDOR_H_
#define SERVIDOR_H_

#include "main.h"

// ----- FUNCIONES -----
void* atender_cliente(void* arg);
void manejar_desconexion_worker(t_worker* w);
void manejar_desconexion_qc(int cfd);


#endif