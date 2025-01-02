#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(smf_display, LOG_LEVEL_DBG);

static const smf_state display_states[];

enum display_state
{
    HPI_DISPLAY_STATE_BOOT,
    HPI_DISPLAY_STATE_ACTIVE,
    HPI_DISPLAY_STATE_SLEEP,
    HPI_DISPLAY_STATE_ON,
    HPI_DISPLAY_STATE_OFF,

};

struct s_object
{
    struct smf_ctx ctx;

} s_obj;

static void st_display_boot_entry(void *o)
{
    LOG_DBG("Display SM Boot Entry");
}

static void st_display_boot_run(void *o)
{
    LOG_DBG("Display SM Boot Run");
}

static void st_display_boot_exit(void *o)
{
    LOG_DBG("Display SM Boot Exit");
}

static void st_display_active_entry(void *o)
{
    LOG_DBG("Display SM Active Entry");
}

static void st_display_active_run(void *o)
{
    LOG_DBG("Display SM Active Run");
}

static void st_display_active_exit(void *o)
{
    LOG_DBG("Display SM Active Exit");
}

static void st_display_sleep_entry(void *o)
{
    LOG_DBG("Display SM Sleep Entry");
}

static void st_display_sleep_run(void *o)
{
    LOG_DBG("Display SM Sleep Run");
}

static void st_display_sleep_exit(void *o)
{
    LOG_DBG("Display SM Sleep Exit");
}

static void st_display_on_entry(void *o)
{
    LOG_DBG("Display SM On Entry");
}

static const struct smf_state display_states[] = {
    [HPI_DISPLAY_STATE_BOOT] = SMF_CREATE_STATE(st_display_boot_entry, st_display_boot_run, st_display_boot_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ACTIVE] = SMF_CREATE_STATE(st_display_active_entry, st_display_active_run, st_display_active_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_SLEEP] = SMF_CREATE_STATE(st_display_sleep_entry, st_display_sleep_run, st_display_sleep_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ON] = SMF_CREATE_STATE(st_display_on_entry, NULL, NULL, NULL, NULL),
};

void smf_display_thread(void)
{
    int ret;

    smf_set_initial(SMF_CTX(&s_obj),&display_states[HPI_DISPLAY_STATE_BOOT]);

    for(;;)
    {
        ret = smf_run_state(SMF_CTX(&s_obj));
        if(ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }
        k_msleep(100);
    }
}