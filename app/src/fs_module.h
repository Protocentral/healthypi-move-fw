#ifndef fs_module_h
#define fs_module_h

#include "sampling_module.h"

void fs_module_init(void);
void init_settings(void);
void record_write_to_file(int current_session_log_id, int current_session_log_counter, struct hpi_ecg_bioz_sensor_data_t *current_session_log_points);

#endif