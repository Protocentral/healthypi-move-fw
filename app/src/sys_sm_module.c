#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/smf.h>

#include "hw_module.h"
#include "display_module.h"
#include "fs_module.h"

#include "sys_sm_module.h"

// static int sess_time = 0;

// Display State Machine - SMF
static const struct smf_state display_states[];

extern const struct device *display_dev;

extern struct k_sem sem_disp_inited;

enum display_state
{
    DISPLAY_STATE_HOME,
};

static enum display_state current_state = DISPLAY_STATE_HOME;

/* User defined object */
struct d_object
{
    /* This must be first */
    struct smf_ctx ctx;

    /* Events */
    struct k_event smf_event;
    int32_t events;

    /* Other state specific data add here */
} d_obj;

void switch_state_home()
{
    smf_set_state(SMF_CTX(&d_obj), &display_states[DISPLAY_STATE_HOME]);
}

static void display_state_home_entry(void *o)
{
    // struct d_object *s = (struct d_object *)o;
    printk("SMF State Entry: HOME\n");

    //draw_scr_charts_all();
    //draw_scr_chart_single();
    //draw_scr_home();

    current_state = DISPLAY_STATE_HOME;
}

static void display_state_home_run(void *o)
{
    // struct d_object *s = (struct d_object *)o;
    //  printk("Display SMF State: (RUN) HOME");

    // Code to keep home screen running
}

static void display_state_home_exit(void *o)
{
    // struct d_object *s = (struct d_object *)o;
    printk("SMF State Exit: HOME\n");

    // TODO: Dispose of screen
}

static const struct smf_state display_states[] =
    {
        [DISPLAY_STATE_HOME] = SMF_CREATE_STATE(display_state_home_entry, display_state_home_run, display_state_home_exit),

};

void sys_sm_thread(void)
{
    int ret = 0;

    // display_screens_init();
    //k_sem_take(&sem_disp_inited, K_FOREVER);

    k_event_init(&d_obj.smf_event);

    /* Set initial SMF state */
    smf_set_initial(SMF_CTX(&d_obj), &display_states[DISPLAY_STATE_HOME]);

    printk("Display Thread Started\n");

    while (1)
    {
        /* State machine terminates if a non-zero value is returned */
        ret = smf_run_state(SMF_CTX(&d_obj));
        if (ret)
        {
            /* handle return code and terminate state machine */
            break;
        }

        k_sleep(K_SECONDS(1));
    }
}

#define SYS_SM_THREAD_STACKSIZE 4096
#define SYS_SM_THREAD_PRIORITY 7

//K_THREAD_DEFINE(sys_smf_thread_id, SYS_SM_THREAD_STACKSIZE, sys_sm_thread, NULL, NULL, NULL, SYS_SM_THREAD_PRIORITY, 0, 1500);