#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smf_ppg, LOG_LEVEL_DBG);

#include "hw_module.h"
#include "maxm86146.h"

static const struct smf_state ppg_samp_states[];
static const struct device *maxm86146_dev = DEVICE_DT_GET_ANY(maxim_maxm86146);

K_SEM_DEFINE(sem_ppg_wrist_thread_start, 0, 1);

extern struct k_sem sem_ppg_sm_start;

enum ppg_samp_state
{
    PPG_SAMP_STATE_ACTIVE,
    PPG_SAMP_STATE_PROBING,
    PPG_SAMP_STATE_OFF_SKIN,
};

struct s_object
{
    struct smf_ctx ctx;

} s_obj;

static void st_ppg_samp_active_entry(void *o)
{
    LOG_DBG("PPG SM Active Entry");

    hw_maxm86146_set_op_mode(MAXM86146_OP_MODE_ALGO_AEC);
}

static void st_ppg_samp_active_run(void *o)
{
    //LOG_DBG("PPG SM Active Run");
}

static void st_ppg_samp_active_exit(void *o)
{
    LOG_DBG("PPG SM Active Exit");
}

static void st_ppg_samp_probing_entry(void *o)
{
    LOG_DBG("PPG SM Probing Entry");

    // Enter SCD mode
    hw_maxm86146_set_op_mode(MAXM86146_OP_MODE_SCD);
}

static void st_ppg_samp_probing_run(void *o)
{
    LOG_DBG("PPG SM Probing Run");
}

static void st_ppg_samp_probing_exit(void *o)
{
    LOG_DBG("PPG SM Probing Exit");
}

static void st_ppg_samp_off_skin_entry(void *o)
{
    LOG_DBG("PPG SM Off Skin Entry");
}

static void st_ppg_samp_off_skin_run(void *o)
{
    LOG_DBG("PPG SM Off Skin Running");
}

static void st_ppg_samp_off_skin_exit(void *o)
{
    LOG_DBG("PPG SM Off Skin Exit");
}

static const struct smf_state ppg_samp_states[] = {
    [PPG_SAMP_STATE_ACTIVE] = SMF_CREATE_STATE(st_ppg_samp_active_entry, st_ppg_samp_active_run, st_ppg_samp_active_exit, NULL, NULL),
    [PPG_SAMP_STATE_PROBING] = SMF_CREATE_STATE(st_ppg_samp_probing_entry, st_ppg_samp_probing_run, st_ppg_samp_probing_exit, NULL, NULL),
    [PPG_SAMP_STATE_OFF_SKIN] = SMF_CREATE_STATE(st_ppg_samp_off_skin_entry, st_ppg_samp_off_skin_run, st_ppg_samp_off_skin_exit, NULL, NULL),
};

void smf_ppg_thread(void)
{
    int32_t ret;

    k_sem_take(&sem_ppg_sm_start, K_FOREVER);

    if (hw_is_maxm86146_present() == false)
    {
        LOG_ERR("MAXM86146 device not present. Not starting PPG SMF");
        return;
    }

    smf_set_initial(SMF_CTX(&s_obj), &ppg_samp_states[PPG_SAMP_STATE_ACTIVE]);

    k_sem_give(&sem_ppg_wrist_thread_start);

    LOG_INF("PPG State Machine Thread starting");

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_obj));
        if (ret)
        {
            LOG_ERR("Error in PPG State Machine");
            break;
        }
        k_msleep(1000);
    }
}

#define SMF_PPG_THREAD_STACKSIZE 4096
#define SMF_PPG_THREAD_PRIORITY 7

K_THREAD_DEFINE(smf_ppg_thread_id, SMF_PPG_THREAD_STACKSIZE, smf_ppg_thread, NULL, NULL, NULL, SMF_PPG_THREAD_PRIORITY, 0, 1000);
