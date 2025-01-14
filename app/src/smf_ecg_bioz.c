#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/sensor.h>

#include "sampling_module.h"
#include "hw_module.h"
#include "ui/move_ui.h"

LOG_MODULE_REGISTER(smf_ecg_bioz, LOG_LEVEL_DBG);

static const struct smf_state ecg_bioz_states[];

struct s_ecg_bioz_object
{
    struct smf_ctx ctx;
} s_ecg_bioz_obj;

enum ecg_bioz_state
{
    HPI_ECG_BIOZ_STATE_INIT,
    HPI_ECG_BIOZ_STATE_IDLE,
    HPI_ECG_BIOZ_STATE_STREAM,
};

// EXTERNS
extern const struct device *const max30001_dev;
extern struct k_sem sem_ecg_bioz_smf_start;

static void st_ecg_bioz_init_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Init Entry");
    // Disable ECG and BIOZ by default
    hw_max30001_ecg_enable(false);
    hw_max30001_bioz_enable(false);
}

static void st_ecg_bioz_idle_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Entry");
}

static void st_ecg_bioz_idle_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Idle Run");
}

static void st_ecg_bioz_idle_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Idle Exit");
}

static void st_ecg_bioz_stream_entry(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Entry");
}

static void st_ecg_bioz_stream_run(void *o)
{
    // LOG_DBG("ECG/BioZ SM Stream Run");
}

static void st_ecg_bioz_stream_exit(void *o)
{
    LOG_DBG("ECG/BioZ SM Stream Exit");
}

static const struct smf_state ecg_bioz_states[] = {
    [HPI_ECG_BIOZ_STATE_INIT] = SMF_CREATE_STATE(st_ecg_bioz_init_entry, NULL, NULL, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_IDLE] = SMF_CREATE_STATE(st_ecg_bioz_idle_entry, st_ecg_bioz_idle_run, st_ecg_bioz_idle_exit, NULL, NULL),
    [HPI_ECG_BIOZ_STATE_STREAM] = SMF_CREATE_STATE(st_ecg_bioz_stream_entry, st_ecg_bioz_stream_run, st_ecg_bioz_stream_exit, NULL, NULL),
};

void smf_ecg_bioz_thread(void)
{
    int ret;

    LOG_DBG("ECG/BioZ SMF Thread Started");

    // Wait for HW module to init ECG/BioZ
    k_sem_take(&sem_ecg_bioz_smf_start, K_FOREVER);

    smf_set_initial(SMF_CTX(&s_ecg_bioz_obj), &ecg_bioz_states[HPI_ECG_BIOZ_STATE_INIT]);

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_ecg_bioz_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }
        k_msleep(100);
    }
}

K_THREAD_DEFINE(smf_ecg_bioz_thread_id, 1024, smf_ecg_bioz_thread, NULL, NULL, NULL, 10, 0, 0);