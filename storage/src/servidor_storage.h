#ifndef SERVIDOR_STORAGE_H
#define SERVIDOR_STORAGE_H

int iniciar_servidor_storage(const char* puerto); 
bool fs_crear_file_tag(uint32_t query_id, const char* punto_montaje, const char* file_tag_name);
ssize_t fs_leer_bloque(uint32_t query_id, const char* punto_montaje, const char* file_tag_name, uint32_t bloque_logico_idx, void* buffer_salida, uint32_t tam_a_leer);
bool fs_truncate(uint32_t query_id, const char* path_logico, uint32_t new_size);
bool fs_write_bloque(uint32_t query_id, const char* path_logico, uint32_t block_idx, const void* data, uint32_t len);
bool fs_commit(uint32_t query_id, const char* path_logico);
bool fs_delete(uint32_t query_id, const char* path_logico);
bool fs_tag(uint32_t query_id, const char* src_path_logico, const char* dst_path_logico);

#endif // SERVIDOR_STORAGE_H