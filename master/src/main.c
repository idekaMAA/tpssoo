#include "../include/main.h"
#include "../include/inicializaciones.h"
#include "../include/servidor.h"
#include "../include/planificador.h"
#include "../include/aging.h"

int main(int argc, char** argv) {
     if (argc < 2) {
        fprintf(stderr, "Uso: %s <config>\n", argv[0]);
        return 1;
    }

    iniciar_logger();
    iniciar_config(argv[1]);
    inicializar_estructuras();  

    pthread_t th_planificador, th_aging;
    pthread_create(&th_planificador, NULL, planificador_loop, NULL);
    pthread_detach(th_planificador);

    pthread_create(&th_aging, NULL, aging_loop, NULL);
    pthread_detach(th_aging);

    char pstr[16]; 
    snprintf(pstr, sizeof(pstr), "%d", config_master.puerto_escucha);
    int lfd = escuchar_en(pstr);
    if (lfd < 0) { 
        log_error(logger, "No pude escuchar en %s", pstr); 
        return 1; 
    }
    log_info(logger, "MASTER escuchando en %s", pstr);

    while (1) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            log_error(logger, "Falló accept()");
            continue;
        }

        int* arg = malloc(sizeof(int));
        *arg = cfd;

        pthread_t th;
        pthread_create(&th, NULL, atender_cliente, arg);
        pthread_detach(th);
    }

    close(lfd);
    destruir_estructuras();
    liberar_estructuras();

    return 0;
}


void destruir_estructuras() {
    list_destroy_and_destroy_elements(workers, free);
    list_destroy_and_destroy_elements(cola_ready, free);
    list_destroy_and_destroy_elements(cola_exec, free);
    list_destroy_and_destroy_elements(cola_exit, free);

    pthread_mutex_destroy(&mutex_workers);
    pthread_mutex_destroy(&mutex_ready);
    pthread_mutex_destroy(&mutex_exec);
    pthread_mutex_destroy(&mutex_exit);

    sem_destroy(&sem_ready);
    sem_destroy(&sem_workers);
}

void liberar_estructuras() {
    // Opcional: config se puede destruir apenas copiamos los datos en la Struct (inicializacion.c)
    if (config != NULL) {
        config_destroy(config);
    }

    if (logger != NULL) {
        log_destroy(logger);
    }

    free(config_master.algoritmo_planificacion);
    free(config_master.log_level);
}
