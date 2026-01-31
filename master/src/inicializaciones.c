#include "../include/inicializaciones.h"

t_log* logger;
t_config* config;
t_config_master config_master;

t_list* workers;
t_list* cola_ready;
t_list* cola_exec;
t_list* cola_exit;
uint32_t id_counter = 0;

pthread_mutex_t mutex_workers;
pthread_mutex_t mutex_ready;
pthread_mutex_t mutex_exec;
pthread_mutex_t mutex_exit;

sem_t sem_ready;
sem_t sem_workers;

void iniciar_logger() {
    logger = log_create("master.log", "MASTER", true, LOG_LEVEL_INFO);
    if (logger == NULL) {
        printf("No se pudo crear el logger\n");
        exit(1);
    }
    log_info(logger, "Logger inicializado correctamente");
}

void iniciar_config(const char* path_cfg) {
    config = config_create((char*) path_cfg);
    if (config == NULL) {
        log_error(logger, "No se pudo crear el config (%s)", path_cfg);
        exit(1);
    }

    config_master.puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    config_master.algoritmo_planificacion = strdup(config_get_string_value(config, "ALGORITMO_PLANIFICACION"));
    config_master.tiempo_aging = config_get_int_value(config, "TIEMPO_AGING");
    config_master.log_level = strdup(config_get_string_value(config, "LOG_LEVEL"));

    log_info(logger,
             "Configuración cargada: PUERTO=%d, ALGORITMO=%s, AGING=%d",
             config_master.puerto_escucha,
             config_master.algoritmo_planificacion,
             config_master.tiempo_aging);
}


void inicializar_estructuras() {
    workers = list_create();
    cola_ready = list_create();
    cola_exec = list_create();
    cola_exit = list_create();

    id_counter = 0;

    pthread_mutex_init(&mutex_workers, NULL);
    pthread_mutex_init(&mutex_ready, NULL);
    pthread_mutex_init(&mutex_exec, NULL);
    pthread_mutex_init(&mutex_exit, NULL);

    sem_init(&sem_ready, 0, 0);  
    sem_init(&sem_workers, 0, 0);
    
    log_info(logger, "Estructuras de datos inicializadas correctamente");
}