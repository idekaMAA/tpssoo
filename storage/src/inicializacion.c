#define _GNU_SOURCE 
#include "inicializacion.h"
#include "config.h"
#include <commons/bitarray.h> 
#include <commons/config.h>
#include "superblock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h> 
#include <sys/types.h>
#include <errno.h>    
#include <fcntl.h>      
#include <sys/mman.h>   
#include <dirent.h>     
#include <commons/collections/list.h> 
#include <commons/log.h>
#include <commons/string.h> 

// --- VARIABLES GLOBALES DE FILE SYSTEM ---
void* g_blocks_dat_map = NULL;
extern storage_config_t g_storage_config;
t_superbloque g_superbloque;
int g_blocks_dat_fd = -1;

// globale de bitmap
void*       g_bitmap_map = NULL;
size_t      g_bitmap_size = 0;
t_bitarray* g_bitmap = NULL;

// --- FUNCIONES AUXILIARES DE MANEJO DE FS ---

// Crea un directorio simple (no recursivo).
bool crear_directorio_single(const char* ruta) {
    int result = mkdir(ruta, 0777); 
    if (result == 0 || errno == EEXIST) {
        return true;
    }
    log_error(g_storage_config.logger, "Error critico de FS al intentar crear directorio %s: %s", ruta, strerror(errno));
    return false;
}

// Crea una ruta de directorios recursivamente.
bool crear_directorio_rescursivo(const char* ruta_completa) {
    if (!ruta_completa || ruta_completa[0] == '\0') {
        return false;
    }

    char tmp[PATH_MAX];
    strncpy(tmp, ruta_completa, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';

    size_t len = strlen(tmp);
    if (len > 0 && tmp[len-1] == '/') tmp[len-1] = '\0';

        for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') { 
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                log_error(g_storage_config.logger, "Fallo mkdir recursivo en %s: %s", tmp, strerror(errno));
                return false;
            }
            *p = '/';
        }
    }
    
    // Crear el directorio final
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
    log_error(g_storage_config.logger, "Fallo mkdir recursivo en %s: %s", tmp, strerror(errno));
        return false;
    }
    
    return true; 
}

// Combina punto_montaje y sub_path para crear un directorio.
bool crear_directorio(const char* base, const char* sub_path){
    char ruta_completa[PATH_MAX];
    snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", base, sub_path);
    if (mkdir(ruta_completa, 0777) == 0) {
        return true;
    } 
    if (errno == EEXIST) {
        return true;
    }
    log_error(g_storage_config.logger, "Fallo al intentar crear directorio %s: %s", ruta_completa, strerror(errno));
    return false;
}


bool crear_archivo(const char* ruta_completa){
    int fd = open(ruta_completa, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if(fd < 0){
        log_error(g_storage_config.logger, "Fallo al crear archivo: %s. Error: %s", ruta_completa, strerror(errno));
        return false;
    }
    close(fd);
    return true;
}

// Crea un archivo y lo rellena con un caracter hasta alcanzar el tamaño.
bool crear_relleno_tam_fijo(const char* ruta_completa, size_t tam, char fill) {
    FILE* f = fopen(ruta_completa, "wb");
    if (f == NULL) {
        log_error(g_storage_config.logger, "Fallo al crear y rellenar archivo %s: %s", ruta_completa, strerror(errno));
        return false;
    }
    
    for (size_t i = 0; i < tam; ++i) {
        if (fputc(fill, f) == EOF) {
            log_error(g_storage_config.logger, "Error escribiendo en %s", ruta_completa);
            fclose(f);
            return false;
        }
    }
    
    fclose(f);
    return true;
}

// Crea un hard link.
bool hard_link(const char* origen, const char* destino){
    int res = link(origen, destino);
    if(res == -1){
        if(errno != EEXIST){
            log_error(g_storage_config.logger, "Fallo hard_link de %s a %s: %s", origen, destino, strerror(errno));
            return false;
        }
    }
    return true;
}

//Elimina el contenido de una ruta recursivamente (para FRESH_START).
bool eliminar_contenido(const char* ruta) {
    DIR *dir = opendir(ruta);
    int res = -1;

    if (!dir) {
        if (errno == ENOENT) return true;
        log_error(g_storage_config.logger, "Fallo al abrir directorio %s: %s", ruta, strerror(errno));
        return false;
    }

    struct dirent *p;
    while ((p = readdir(dir)) != NULL) {
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

        char *buf;
        res = asprintf(&buf, "%s/%s", ruta, p->d_name); 
        if (res == -1) continue;

        if (p->d_type == DT_DIR) {
            if (!eliminar_contenido(buf)) {
                log_error(g_storage_config.logger, "Fallo al eliminar subdirectorio %s", buf);
                free(buf);
                closedir(dir);
                return false;
            }
        } else {
            if (unlink(buf) == -1) {
                log_error(g_storage_config.logger, "Fallo al eliminar archivo %s: %s", buf, strerror(errno));
                free(buf);
                closedir(dir);
                return false;
            }
        }
        free(buf);
    }
    
    closedir(dir);
    res = rmdir(ruta); 
    if (res == -1 && errno != ENOENT) {
        log_error(g_storage_config.logger, "Fallo al eliminar directorio %s: %s", ruta, strerror(errno));
        return false;
    }
    
    return true;
}


// --- LÓGICA DE CREACIÓN DE ARCHIVOS BASE DEL FS ---

bool crear_superbloque(const char* punto_montaje) {
    char* ruta_superblock = string_from_format("%s/Metadata/Superblock.bin", punto_montaje);
    
    char fs_size_str[32];
    char block_size_str[32];
    snprintf(fs_size_str, sizeof(fs_size_str), "%u", g_storage_config.fs_size);
    snprintf(block_size_str, sizeof(block_size_str), "%u", g_storage_config.block_size);

    char* contenido = string_from_format("FS_SIZE=%s\nBLOCK_SIZE=%s\n", fs_size_str, block_size_str);
    
    FILE* f = fopen(ruta_superblock, "w");
    if (f == NULL) {
        log_error(g_storage_config.logger, "Fallo al crear y escribir Superblock.bin en %s: %s", ruta_superblock, strerror(errno));
        free(ruta_superblock);
        free(contenido);
        return false;
    }
    
    if (fputs(contenido, f) == EOF) {
        log_error(g_storage_config.logger, "Fallo al escribir contenido en Superblock.bin.");
        fclose(f);
        free(ruta_superblock);
        free(contenido);
        return false;
    }
    
    fclose(f);
    
    free(contenido);
    log_info(g_storage_config.logger, "Creado Superblock.bin en %s con FS_SIZE=%u, BLOCK_SIZE=%u. (Escritura directa)", ruta_superblock, g_storage_config.fs_size, g_storage_config.block_size);
    free(ruta_superblock);
    return true;
}

bool crear_bitmap(const char* punto_montaje) {
    char* ruta_bitmap = string_from_format("%s/Metadata/Bitmap.bin", punto_montaje);

    uint32_t num_bloques = g_storage_config.fs_size / g_storage_config.block_size;
    uint32_t tam_bytes = (num_bloques + 7) / 8;

    // Siempre inicializar en cero
    void* array_bytes = calloc(tam_bytes, 1);
    if (!array_bytes) {
        log_error(g_storage_config.logger, "Fallo al reservar memoria para Bitmap.");
        free(ruta_bitmap);
        return false;
    }

    // El bitarray solo se usa PARA ESCRIBIR
    t_bitarray* bitmap = bitarray_create_with_mode(array_bytes, tam_bytes, LSB_FIRST);

    FILE* f = fopen(ruta_bitmap, "wb");
    if (!f) {
        log_error(g_storage_config.logger, "Fallo al crear Bitmap.bin: %s", strerror(errno));
        bitarray_destroy(bitmap);
        free(ruta_bitmap);
        return false;
    }

    size_t escritos = fwrite(array_bytes, 1, tam_bytes, f);
    fclose(f);

    bitarray_destroy(bitmap);

    if (escritos != tam_bytes) {
        log_error(g_storage_config.logger, "Fallo al escribir el contenido completo del Bitmap.bin.");
        free(ruta_bitmap);
        return false;
    }
    log_info(g_storage_config.logger, "Creado Bitmap.bin de %u bloques (tamaño %u bytes).", num_bloques, tam_bytes);

    free(ruta_bitmap);
    return true;
}


bool crear_bloques_fisicos(const char* punto_montaje) {
    char* ruta_data = string_from_format("%s/physical_blocks/Data.bin", punto_montaje);
    uint32_t tam_total = g_storage_config.fs_size;
    
    log_info(g_storage_config.logger, "Creando archivo único Data.bin de %u bytes...", tam_total);

    if (!crear_relleno_tam_fijo(ruta_data, tam_total, '\0')) {
        log_error(g_storage_config.logger, "Fallo al crear Data.bin.");
        free(ruta_data);
        return false;
    }
    
    free(ruta_data);
    log_info(g_storage_config.logger, "Creación de bloques físicos completada en Data.bin.");
    return true;
}

bool fs_mapear_data_blocks(const char* punto_montaje) {
    char* ruta_data = string_from_format("%s/physical_blocks/Data.bin", punto_montaje);
    uint32_t tam_total = g_storage_config.fs_size;

    g_blocks_dat_fd = open(ruta_data, O_RDWR, 0);
    free(ruta_data);

    if (g_blocks_dat_fd == -1) {
        log_error(g_storage_config.logger, "Error al abrir Data.bin: %s", strerror(errno));
        return false;
    }

    g_blocks_dat_map = mmap(NULL, tam_total, PROT_READ | PROT_WRITE, MAP_SHARED, g_blocks_dat_fd, 0);

    if (g_blocks_dat_map == MAP_FAILED) {
        log_error(g_storage_config.logger, "Error al mapear Data.bin a memoria (mmap): %s", strerror(errno));
        close(g_blocks_dat_fd);
        g_blocks_dat_fd = -1;
        return false;
    }
    
    log_info(g_storage_config.logger, "Data.bin mapeado a memoria exitosamente. Tamaño: %u bytes.", tam_total);
    return true;
}

void fs_desmapear_data_blocks(void) {
    if (g_blocks_dat_map != NULL && g_blocks_dat_map != MAP_FAILED) {
        munmap(g_blocks_dat_map, g_storage_config.fs_size);
        g_blocks_dat_map = NULL;
    }
    if (g_blocks_dat_fd != -1) {
        close(g_blocks_dat_fd);
        g_blocks_dat_fd = -1;
    }
}

// --- FUNCIONES PRINCIPALES DE INICIALIZACIÓN ---

bool init_nuevo_fs(const char* punto_montaje) {
    
    log_info(g_storage_config.logger, "Iniciando FRESH_START para File System en: %s", punto_montaje);

    if (!eliminar_contenido(punto_montaje)) {
        log_error(g_storage_config.logger, "Fallo la limpieza previa del punto de montaje en FRESH_START.");
        return false;
    }
    if (!crear_directorio_rescursivo(punto_montaje)) {
        log_error(g_storage_config.logger, "Fallo al crear o acceder al punto de montaje: %s", punto_montaje);
        return false;
    }    
    log_info(g_storage_config.logger, "Creando estructura de directores (Metadata, physical_blocks, files)...");

    if (!crear_directorio(punto_montaje, "Metadata")) return false; 
    if (!crear_directorio(punto_montaje, "physical_blocks")) return false; 
    if (!crear_directorio(punto_montaje, "files")) return false; 
    
    log_info(g_storage_config.logger, "Estructura de directorios base creada exitosamente.");
    
    if (!crear_superbloque(punto_montaje)) return false;
    if (!crear_bitmap(punto_montaje)) return false;


    // INICIALIZO LOS CAMPOS DEL SUPERBLOQUE
    g_superbloque.tam_fs_bytes     = g_storage_config.fs_size;
    g_superbloque.tam_bloque_bytes = g_storage_config.block_size;
    g_superbloque.cantidad_bloques = g_storage_config.fs_size / g_storage_config.block_size;

    log_info(g_storage_config.logger, "Superbloque inicializado: FS=%llu bytes, BLOCK_SIZE=%u, BLOQUES=%u", (unsigned long long) g_superbloque.tam_fs_bytes, g_superbloque.tam_bloque_bytes, g_superbloque.cantidad_bloques);


    // CARGAR SUPERBLOCK DESDE DISCO
    char* ruta_super = string_from_format("%s/Metadata/Superblock.bin", punto_montaje);

    if (!superbloque_cargar_config(ruta_super, &g_superbloque)) {
        log_error(g_storage_config.logger, "ERROR: no se pudo cargar Superblock.bin");
        free(ruta_super);
        return false;
    }

    free(ruta_super);
    if (!fs_mapear_bitmap(punto_montaje)) return false;     
    if (!crear_bloques_fisicos(punto_montaje)) return false;    
    if (!fs_mapear_data_blocks(punto_montaje)) return false;
    if (!hash_index_init(punto_montaje, g_superbloque.cantidad_bloques)) {
        log_error(g_storage_config.logger, "Error inicializando blocks_hash_index.");
        return false;
    }

    log_info(g_storage_config.logger, "Inicializacion de FS nuevo completada exitosamente.");
    return true;
}


bool fs_actual(const char* punto_montaje) {
    log_info(g_storage_config.logger, "Cargando FS existente: %s", punto_montaje);    
    if (!fs_mapear_data_blocks(punto_montaje)) return false;

    g_superbloque.tam_fs_bytes     = g_storage_config.fs_size;
    g_superbloque.tam_bloque_bytes = g_storage_config.block_size;
    g_superbloque.cantidad_bloques = g_storage_config.fs_size / g_storage_config.block_size;

    if (!fs_mapear_bitmap(punto_montaje)) return false;         
    log_info(g_storage_config.logger, "Carga de FS existente completada exitosamente.");
    
    // === HASH INDEX INIT ===
    if (!hash_index_init(punto_montaje, g_superbloque.cantidad_bloques)) {
        log_error(g_storage_config.logger, "Error inicializando blocks_hash_index.");
        return false;
    }
    return true; 
}

bool crear_init_arch(const char* punto_montaje) {
    log_info(g_storage_config.logger, "Simulando creación de archivo inicial /files/File:Tag/metadata.config");
    
    // Ruta del directorio files/File:Tag
    char* ruta_tag_directorio = string_from_format("%s/files/initial_file/BASE", punto_montaje);
    
    // Asegura que el directorio exista
    if (!crear_directorio_rescursivo(ruta_tag_directorio)) {
        log_error(g_storage_config.logger, "Fallo al crear directorio inicial: %s", ruta_tag_directorio);
        free(ruta_tag_directorio);
        return false;
    }
    
    // Definir la ruta completa del metadata.config
    char* ruta_metadata = string_from_format("%s/metadata.config", ruta_tag_directorio);
    
    // Crear el archivo vacío antes de config_create()
    if (!crear_archivo(ruta_metadata)) {
        log_error(g_storage_config.logger, "Fallo al crear el archivo metadata.config en %s", ruta_metadata);
        free(ruta_metadata);
        free(ruta_tag_directorio);
        return false;
    }
    
    // Crear t_config 
    t_config* metadata = config_create(ruta_metadata); 

    if (metadata != NULL) {
        config_set_value(metadata, "ESTADO", "COMMITED");
        config_set_value(metadata, "BLOCKS", "[0]"); 
        
        char tam_str[32];
        snprintf(tam_str, sizeof(tam_str), "%u", g_storage_config.block_size);
        config_set_value(metadata, "TAMANO", tam_str);
        
        config_save(metadata); 
        config_destroy(metadata);
        
        log_info(g_storage_config.logger, "Archivo inicial /files/File:Tag/metadata.config creado.");
    } else {
        // En este punto, si falla, es un error más serio que ENOENT
        log_error(g_storage_config.logger, "Fallo al crear t_config para el archivo de metadatos %s (Archivo ya debería existir)", ruta_metadata);
        free(ruta_metadata);
        free(ruta_tag_directorio);
        return false;
    }
    
    free(ruta_metadata);
    free(ruta_tag_directorio);
    return true;
}

bool fs_mapear_bitmap(const char* punto_montaje) {
    char* ruta_bitmap = string_from_format("%s/Metadata/Bitmap.bin", punto_montaje);

    int fd = open(ruta_bitmap, O_RDWR);
    if (fd == -1) {
        log_error(g_storage_config.logger, "No se pudo abrir Bitmap.bin: %s", strerror(errno));
        free(ruta_bitmap);
        return false;
    }

    // tamaño esperado según superblock
    uint32_t num_bloques = g_storage_config.fs_size / g_storage_config.block_size;
    size_t tam_esperado = (num_bloques + 7) / 8;

    // tamaño real del archivo
    struct stat st; 
    if (fstat(fd, &st) == -1) {
        log_error(g_storage_config.logger, "fstat Bitmap.bin falló: %s", strerror(errno));
        close(fd); free(ruta_bitmap);
        return false;
    }

    if ((size_t)st.st_size != tam_esperado) {
        log_error(g_storage_config.logger, "Bitmap.bin tamaño inválido. Esperado=%zu, real=%zu",
                (size_t)tam_esperado, (size_t)st.st_size);
        close(fd); free(ruta_bitmap);
        return false;
    }

    void* map = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        log_error(g_storage_config.logger, "mmap Bitmap.bin falló: %s", strerror(errno));
        free(ruta_bitmap);
        return false;
    }

    // envolver con bitarray
    t_bitarray* ba = bitarray_create_with_mode((char*)map, st.st_size, LSB_FIRST);
    if (!ba) {
        log_error(g_storage_config.logger, "bitarray_create_with_mode falló");
        munmap(map, st.st_size);
        free(ruta_bitmap);
        return false;
    }

    g_bitmap_map  = map;
    g_bitmap_size = st.st_size;
    g_bitmap      = ba;

    //uint8_t* bytes = (uint8_t*) g_bitmap_map;
    log_info(g_storage_config.logger, "Bitmap.bin mapeado (%zu bytes, %u bloques).", g_bitmap_size, num_bloques);
    free(ruta_bitmap);
    return true;
}

void fs_desmapear_bitmap(void) {
    if (g_bitmap) {
        bitarray_destroy(g_bitmap);  
        g_bitmap = NULL;
    }
    if (g_bitmap_map && g_bitmap_map != MAP_FAILED) {
        msync(g_bitmap_map, g_bitmap_size, MS_SYNC);
        munmap(g_bitmap_map, g_bitmap_size);
        g_bitmap_map = NULL;
    }
    g_bitmap_size = 0;
}