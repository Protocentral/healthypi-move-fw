#ifndef ACC_RECORDER_H
#define ACC_RECORDER_H

#include <stdint.h>
#include <zephyr/kernel.h>

enum acc_rec_state {
	ACC_REC_IDLE = 0,
	ACC_REC_OPENING,   /* configuring IMU + creating the file */
	ACC_REC_RECORDING,
	ACC_REC_CLOSING,   /* flushing + closing the file */
	ACC_REC_ERROR,     /* last session failed; see acc_rec_get_error() */
};

#define ACC_SAMPLE_HZ 20  

struct acc_sample {
	uint32_t t_ms;
	float x, y, z;                  /* m/s^2, gravity included */
};


extern struct k_msgq acc_rec_q; 

/* Non-blocking requests; the recorder thread does the work. */
void acc_rec_request_start(void);
void acc_rec_request_stop(void);

enum acc_rec_state acc_rec_get_state(void);
uint32_t acc_rec_get_samples(void);
uint32_t acc_rec_get_elapsed_s(void);
const char *acc_rec_get_filename(void); /* current / last file, no directory ("" if none) */
const char *acc_rec_get_error(void);
void acc_rec_dump_file(const char *path);
#endif