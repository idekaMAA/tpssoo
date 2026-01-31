#ifndef INICIALIZACIONES_H_
#define INICIALIZACIONES_H_

#include "main.h"

// Structs
typedef struct {
    int puerto_escucha;
    char* algoritmo_planificacion;
    int tiempo_aging;
    char* log_level;
} t_config_master;

// Variables globales
extern t_log* logger;
extern t_config* config;
extern t_config_master config_master; 

// ----- VARIABLES GLOBALES -----
extern t_list* workers;
extern t_list* cola_ready;
extern t_list* cola_exec;
extern t_list* cola_exit;
extern uint32_t id_counter;

// ----- SINCRONIZACIÓN -----
extern pthread_mutex_t mutex_workers;
extern pthread_mutex_t mutex_ready;
extern pthread_mutex_t mutex_exec;
extern pthread_mutex_t mutex_exit;

extern sem_t sem_ready;  
extern sem_t sem_workers; 

// Funciones
void iniciar_logger();
void iniciar_config();
void inicializar_estructuras();

#endif
