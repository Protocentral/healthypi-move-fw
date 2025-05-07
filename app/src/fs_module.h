#ifndef fs_module_h
#define fs_module_h

#include "hpi_common_types.h"
#include "trends.h"

void fs_module_init(void);
void transfer_send_file(char* in_file_name);

void fs_load_file_to_buffer(char *m_file_name, uint8_t *buffer, uint32_t buffer_len);
void fs_write_buffer_to_file(char *m_file_name, uint8_t *buffer, uint32_t buffer_len);

#endif