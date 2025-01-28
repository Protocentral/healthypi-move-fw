#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>

#include "sampling_module.h"
#include "hw_module.h"
#include "ui/move_ui.h"
#include "bmi323_hpi.h"

LOG_MODULE_REGISTER(smf_imu, LOG_LEVEL_INF);

static const struct smf_state imu_states[];

struct sensor_trigger imu_trig = {
    .type = SENSOR_TRIG_DATA_READY,
    .chan = SENSOR_CHAN_ACCEL_XYZ,
};

enum imu_state
{
    HPI_IMU_STATE_INIT,
    HPI_IMU_STATE_IDLE,
    HPI_IMU_STATE_ON_WRIST,
    HPI_IMU_STATE_STREAM,
};

struct s_imu_object
{
    struct smf_ctx ctx;
} s_imu_obj;

// EXTERNS

extern const struct device *imu_dev;
extern struct k_sem sem_imu_smf_start;

static void st_imu_idle_entry(void *o)
{
    LOG_DBG("IMU SM Idle Entry");
}

static void st_imu_idle_run(void *o)
{
    // LOG_DBG("IMU SM Idle Run");
}

static void st_imu_idle_exit(void *o)
{
    LOG_DBG("IMU SM Idle Exit");
}

static void st_imu_on_wrist_entry(void *o)
{
    LOG_DBG("IMU SM On Wrist Entry");
}

static void st_imu_on_wrist_run(void *o)
{
    // LOG_DBG("IMU SM On Wrist Run");
}

static void st_imu_on_wrist_exit(void *o)
{
    LOG_DBG("IMU SM On Wrist Exit");
}

static void st_imu_stream_entry(void *o)
{
    LOG_DBG("IMU SM Stream Entry");
}

static void st_imu_stream_run(void *o)
{
    // LOG_DBG("IMU SM Stream Run");
}

static void st_imu_stream_exit(void *o)
{
    LOG_DBG("IMU SM Stream Exit");
}

static void imu_trigger_handler(const struct device *dev, const struct sensor_trigger *trigger)
{
    ARG_UNUSED(trigger);

    if (sensor_sample_fetch(dev))
    {
        printf("Acc Trig\n");
        return;
    }

    // k_sem_give(&sem);
}

static void st_imu_init_entry(void *o)
{
    LOG_DBG("IMU SM Init Entry");
    if (sensor_trigger_set(imu_dev, &imu_trig, imu_trigger_handler) < 0)
    {
        LOG_ERR("Could not set trigger");
        // return 0;
    }
    else
    {
        LOG_INF("IMU Trigger set");
        struct sensor_value set_val;
        set_val.val1 = 1;
        sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, BMI323_HPI_ATTR_EN_FEATURE_ENGINE, &set_val);
        sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, BMI323_HPI_ATTR_EN_STEP_COUNTER, &set_val);
    }
}


static const struct smf_state imu_states[] = {
    [HPI_IMU_STATE_INIT] = SMF_CREATE_STATE(st_imu_init_entry, NULL,NULL, NULL, NULL),
    [HPI_IMU_STATE_IDLE] = SMF_CREATE_STATE(st_imu_idle_entry, st_imu_idle_run, st_imu_idle_exit, NULL, NULL),
    [HPI_IMU_STATE_ON_WRIST] = SMF_CREATE_STATE(st_imu_on_wrist_entry, st_imu_on_wrist_run, st_imu_on_wrist_exit, NULL, NULL),
    [HPI_IMU_STATE_STREAM] = SMF_CREATE_STATE(st_imu_stream_entry, st_imu_stream_run, st_imu_stream_exit, NULL, NULL),
};

void smf_imu_thread(void)
{
    int ret;

    LOG_INF("IMU SMF Thread Started");

    // Wait for HW module to init IMU
    k_sem_take(&sem_imu_smf_start, K_FOREVER);

    smf_set_initial(SMF_CTX(&s_imu_obj), &imu_states[HPI_IMU_STATE_INIT]);

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_imu_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }
        k_msleep(100);
    }
}

#define IMU_THREAD_STACK_SIZE 1024
#define IMU_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_imu_thread_id, IMU_THREAD_STACK_SIZE, smf_imu_thread, NULL, NULL, NULL, IMU_THREAD_PRIORITY, 0, 0);
