#ifndef fs_module_h
#define fs_module_h

#include "hpi_common_types.h"
#include "trends.h"

void fs_module_init(void);

void transfer_send_file(uint16_t file_id);

#endif