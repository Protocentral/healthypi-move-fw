#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/display.h>
#include <lvgl.h>

#include <display_sh8601.h>

#include "ui/move_ui.h"

LOG_MODULE_REGISTER(smf_display, LOG_LEVEL_DBG);

static const struct smf_state display_states[];

extern struct k_sem sem_display_on;

extern const struct device *display_dev;

extern struct k_sem sem_disp_boot_complete;

enum display_state
{
    HPI_DISPLAY_STATE_INIT,
    HPI_DISPLAY_STATE_BOOT,
    HPI_DISPLAY_STATE_ACTIVE,
    HPI_DISPLAY_STATE_SLEEP,
    HPI_DISPLAY_STATE_ON,
    HPI_DISPLAY_STATE_OFF,

};

struct s_disp_object
{
    struct smf_ctx ctx;

} s_disp_obj;

static void st_display_init_entry(void *o)
{
    LOG_DBG("Display SM Init Entry");

    printk("Disp ON");

    if (!device_is_ready(display_dev))
    {
        LOG_ERR("Device not ready");
        // return;
    }

    LOG_DBG("Display device: %s", display_dev->name);

    sh8601_reinit(display_dev);

    //  Init all styles globally
    display_init_styles();

    display_blanking_off(display_dev);
    hpi_disp_set_brightness(50);

    smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_BOOT]);
}

static void st_display_boot_entry(void *o)
{
    LOG_DBG("Display SM Boot Entry");
    draw_scr_boot();
    lv_task_handler();
}

static void st_display_boot_run(void *o)
{
    // Stay in this state until the boot is complete
    if(k_sem_take(&sem_disp_boot_complete, K_NO_WAIT) == 0)
    {
        smf_set_state(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_ACTIVE]);
    }
    // LOG_DBG("Display SM Boot Run");
}

static void st_display_boot_exit(void *o)
{
    LOG_DBG("Display SM Boot Exit");
}

static void st_display_active_entry(void *o)
{
    LOG_DBG("Display SM Active Entry");
    draw_scr_ppg(SCROLL_RIGHT);
}

static void st_display_active_run(void *o)
{
    //LOG_DBG("Display SM Active Run");
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
    [HPI_DISPLAY_STATE_INIT] = SMF_CREATE_STATE(st_display_init_entry, NULL, NULL, NULL, NULL),
    [HPI_DISPLAY_STATE_BOOT] = SMF_CREATE_STATE(st_display_boot_entry, st_display_boot_run, st_display_boot_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ACTIVE] = SMF_CREATE_STATE(st_display_active_entry, st_display_active_run, st_display_active_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_SLEEP] = SMF_CREATE_STATE(st_display_sleep_entry, st_display_sleep_run, st_display_sleep_exit, NULL, NULL),
    [HPI_DISPLAY_STATE_ON] = SMF_CREATE_STATE(st_display_on_entry, NULL, NULL, NULL, NULL),
};

void smf_display_thread(void)
{
    int ret;

    k_sem_take(&sem_display_on, K_FOREVER);

    LOG_DBG("Display SMF Thread Started");

    smf_set_initial(SMF_CTX(&s_disp_obj), &display_states[HPI_DISPLAY_STATE_INIT]);

    for (;;)
    {
        ret = smf_run_state(SMF_CTX(&s_disp_obj));
        if (ret != 0)
        {
            LOG_ERR("SMF Run error: %d", ret);
            break;
        }
        k_msleep(lv_task_handler());
        // k_msleep(100);
    }
}

#define SMF_DISPLAY_THREAD_STACK_SIZE 32768
#define SMF_DISPLAY_THREAD_PRIORITY 5

K_THREAD_DEFINE(smf_display_thread_id, SMF_DISPLAY_THREAD_STACK_SIZE, smf_display_thread, NULL, NULL, NULL, SMF_DISPLAY_THREAD_PRIORITY, 0, 0);