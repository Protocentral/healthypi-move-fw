// #include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/zbus/zbus.h>

#include "max30001.h"
#include "max32664d.h"
#include "max32664c.h"
#include "sampling_module.h"

LOG_MODULE_REGISTER(sampling_module, CONFIG_SENSOR_LOG_LEVEL);

#define SAMPLING_INTERVAL_MS 8


bool ppg_wrist_sampling_on = false;
bool ppg_finger_sampling_on = false;

// static lv_timer_t *ecg_sampling_timer;

K_SEM_DEFINE(sem_ppg_finger_sample_trigger, 0, 1);

// *** Externs ***
ZBUS_CHAN_DECLARE(hr_chan);







